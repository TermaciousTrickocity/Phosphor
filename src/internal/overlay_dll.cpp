#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstring>
#include <string>

#include "MinHook.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_dx12.h"

#include "mcc.hpp"
#include "objtypes.hpp"
#include "overlay.hpp"
#include "overlay_shared.hpp"
#include "heatmap.hpp"

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn  = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecFn    = void(__stdcall*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

static PresentFn oPresent = nullptr;
static ResizeFn  oResize  = nullptr;
static ExecFn    oExec    = nullptr;

enum class Backend { Unknown, D3D11, D3D12 };
static Backend g_backend = Backend::Unknown;

static ID3D11Device*           g_dev = nullptr;
static ID3D11DeviceContext*    g_ctx = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;

struct FrameCtx {
    ID3D12CommandAllocator*     alloc    = nullptr;
    ID3D12Resource*             rt       = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv      = {};
    UINT64                      fenceVal = 0;
};
static ID3D12Device*              g12_dev     = nullptr;
static ID3D12DescriptorHeap*      g12_rtvHeap = nullptr;
static ID3D12DescriptorHeap*      g12_srvHeap = nullptr;
static ID3D12GraphicsCommandList* g12_list    = nullptr;
static FrameCtx*                  g12_frames  = nullptr;
static UINT                       g12_count   = 0;
static bool                       g12_listOpen = false;
static DXGI_FORMAT                g12_fmt     = DXGI_FORMAT_UNKNOWN;

static ID3D12Fence* g12_fence      = nullptr;
static UINT64       g12_fenceVal   = 0;
static HANDLE       g12_fenceEvent = nullptr;

static ID3D12CommandQueue* g12_cands[8] = {};
static volatile LONG       g12_candN    = 0;
static ID3D12CommandQueue* g12_queue    = nullptr;

static IDXGISwapChain* g12_sc = nullptr;

static HWND     g_hwnd = nullptr;
static bool     g_init = false;
static volatile LONG g_initGuard = 0;
static HMODULE  g_self = nullptr;

static MccReader   g_reader;
static PoolSnapshot g_snap;
static bool        g_attached = false;

static HANDLE g_unloadEvent = nullptr;
static bool   g_cleaned = false;

static ovl::Settings* g_settings = nullptr;

static Heatmap        g_heat;
static HeatmapSettings g_heatSettings = { 48, 3.0f, 1.0f, 0.4f, false };
static uint64_t       g_lastTick = 0;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static std::string dllDir() {
    char p[MAX_PATH] = {};
    GetModuleFileNameA(g_self, p, MAX_PATH);
    std::string s(p);
    const size_t n = s.find_last_of("\\/");
    if (n != std::string::npos) s.resize(n + 1);
    return s;
}

static void createRTV(IDXGISwapChain* sc) {
    ID3D11Texture2D* bb = nullptr;
    if (SUCCEEDED(sc->GetBuffer(0, IID_PPV_ARGS(&bb))) && bb) {
        g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
        bb->Release();
    }
}

static void flushGpu12() {
    if (!g12_queue || !g12_fence || !g12_fenceEvent) return;
    const UINT64 target = ++g12_fenceVal;
    if (FAILED(g12_queue->Signal(g12_fence, target))) return;
    if (g12_fence->GetCompletedValue() >= target) return;
    if (SUCCEEDED(g12_fence->SetEventOnCompletion(target, g12_fenceEvent)))
        WaitForSingleObject(g12_fenceEvent, 1000);
}

static void releaseTargets12() {
    if (!g12_frames) return;
    flushGpu12();
    for (UINT i = 0; i < g12_count; ++i) {
        if (g12_frames[i].rt) { g12_frames[i].rt->Release(); g12_frames[i].rt = nullptr; }
    }
}

static void createTargets12(IDXGISwapChain* sc) {
    if (!g12_dev || !g12_frames) return;
    for (UINT i = 0; i < g12_count; ++i) {
        ID3D12Resource* buf = nullptr;
        if (SUCCEEDED(sc->GetBuffer(i, IID_PPV_ARGS(&buf))) && buf) {
            g12_dev->CreateRenderTargetView(buf, nullptr, g12_frames[i].rtv);
            g12_frames[i].rt = buf;
        }
    }
}

static void shutdown12() {
    releaseTargets12();
    if (g12_frames) {
        for (UINT i = 0; i < g12_count; ++i)
            if (g12_frames[i].alloc) g12_frames[i].alloc->Release();
        delete[] g12_frames;
        g12_frames = nullptr;
    }
    if (g12_list)    { g12_list->Release();    g12_list = nullptr; }
    if (g12_srvHeap) { g12_srvHeap->Release(); g12_srvHeap = nullptr; }
    if (g12_rtvHeap) { g12_rtvHeap->Release(); g12_rtvHeap = nullptr; }
    if (g12_fence)   { g12_fence->Release();   g12_fence = nullptr; }
    if (g12_fenceEvent) { CloseHandle(g12_fenceEvent); g12_fenceEvent = nullptr; }
    if (g12_dev)     { g12_dev->Release();     g12_dev = nullptr; }
    if (g12_sc)      { g12_sc->Release();      g12_sc = nullptr; }
    g12_queue = nullptr;
    g12_count = 0;
    g12_listOpen = false;
    g12_fenceVal = 0;
    g12_fmt = DXGI_FORMAT_UNKNOWN;
}

static ID3D12CommandQueue* pickQueue12(ID3D12Device* dev) {
    const LONG n = g12_candN;
    for (LONG i = 0; i < n && i < 8; ++i) {
        ID3D12CommandQueue* q = g12_cands[i];
        if (!q) continue;
        ID3D12Device* qdev = nullptr;
        if (SUCCEEDED(q->GetDevice(IID_PPV_ARGS(&qdev))) && qdev) {
            const bool match = (qdev == dev);
            qdev->Release();
            if (match) return q;
        }
    }
    return nullptr;
}

static bool init12(IDXGISwapChain* sc, const DXGI_SWAP_CHAIN_DESC& d) {
    if (FAILED(sc->GetDevice(IID_PPV_ARGS(&g12_dev))) || !g12_dev) return false;

    g12_queue = pickQueue12(g12_dev);
    if (!g12_queue) { g12_dev->Release(); g12_dev = nullptr; return false; }

    if (FAILED(g12_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g12_fence))))
        return false;
    g12_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g12_fenceEvent) return false;

    g12_fmt = d.BufferDesc.Format;
    g12_count = d.BufferCount;
    if (g12_count == 0 || g12_count > 8) g12_count = 3;

    D3D12_DESCRIPTOR_HEAP_DESC rd = {};
    rd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rd.NumDescriptors = g12_count;
    rd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(g12_dev->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&g12_rtvHeap)))) return false;

    D3D12_DESCRIPTOR_HEAP_DESC sd = {};
    sd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sd.NumDescriptors = 1;
    sd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(g12_dev->CreateDescriptorHeap(&sd, IID_PPV_ARGS(&g12_srvHeap)))) return false;

    const UINT stride = g12_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = g12_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    g12_frames = new FrameCtx[g12_count];
    for (UINT i = 0; i < g12_count; ++i) {
        g12_frames[i] = FrameCtx{};
        g12_frames[i].rtv = h;
        h.ptr += stride;
        if (FAILED(g12_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(&g12_frames[i].alloc))))
            return false;
    }
    if (FAILED(g12_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          g12_frames[0].alloc, nullptr,
                                          IID_PPV_ARGS(&g12_list))))
        return false;
    g12_list->Close();
    createTargets12(sc);

    sc->AddRef();
    g12_sc = sc;

    return ImGui_ImplDX12_Init(g12_dev, int(g12_count), g12_fmt, g12_srvHeap,
                               g12_srvHeap->GetCPUDescriptorHandleForHeapStart(),
                               g12_srvHeap->GetGPUDescriptorHandleForHeapStart());
}

static void render12(IDXGISwapChain* sc) {
    if (!g12_list || !g12_frames || !g12_queue || sc != g12_sc) return;

    IDXGISwapChain3* sc3 = nullptr;
    if (FAILED(sc->QueryInterface(IID_PPV_ARGS(&sc3))) || !sc3) return;
    const UINT idx = sc3->GetCurrentBackBufferIndex();
    sc3->Release();

    if (idx >= g12_count) return;
    FrameCtx& f = g12_frames[idx];
    if (!f.alloc || !f.rt) return;

    if (f.fenceVal && g12_fence->GetCompletedValue() < f.fenceVal) {
        if (FAILED(g12_fence->SetEventOnCompletion(f.fenceVal, g12_fenceEvent))) return;
        if (WaitForSingleObject(g12_fenceEvent, 1000) != WAIT_OBJECT_0) return;
    }

    if (g12_listOpen) {
        if (FAILED(g12_list->Close())) {
            g12_list->Release(); g12_list = nullptr;
            if (FAILED(g12_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  f.alloc, nullptr, IID_PPV_ARGS(&g12_list))))
                return;
            g12_list->Close();
        }
        g12_listOpen = false;
    }

    if (FAILED(f.alloc->Reset())) return;
    if (FAILED(g12_list->Reset(f.alloc, nullptr))) return;
    g12_listOpen = true;

    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    b.Transition.pResource   = f.rt;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g12_list->ResourceBarrier(1, &b);

    g12_list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
    g12_list->SetDescriptorHeaps(1, &g12_srvHeap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g12_list);

    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    g12_list->ResourceBarrier(1, &b);
    if (FAILED(g12_list->Close())) return;
    g12_listOpen = false;

    ID3D12CommandList* lists[] = { g12_list };
    if (oExec) oExec(g12_queue, 1, lists);
    else       g12_queue->ExecuteCommandLists(1, lists);

    if (SUCCEEDED(g12_queue->Signal(g12_fence, ++g12_fenceVal)))
        f.fenceVal = g12_fenceVal;
}

static void drawMarkers(float vw, float vh) {
    if (!g_attached) return;
    if (!g_reader.sample(g_snap)) { g_reader.detach(); g_attached = false; return; }

    const uint64_t nowT = GetTickCount64();
    const float dt = g_lastTick ? float(nowT - g_lastTick) / 1000.0f : 0.016f;
    g_lastTick = nowT;

    ovl::Settings s;
    if (g_settings && g_settings->magic == ovl::kMagic) s = *g_settings;
    else ovl::initDefaults(s);

    if (s.heatCell > 0.0f) g_heatSettings.cellSize = s.heatCell;
    if (s.heatCols > 0)    g_heatSettings.columns  = s.heatCols;

    const uint32_t elems = s.elements;
    const bool needAttach = (elems & ovl::Attachment) != 0;
    const bool needTag = (elems & (ovl::TagName | ovl::TagPath)) != 0 || needAttach;
    const float minR2 = s.minRange * s.minRange;
    const float maxR2 = s.maxRange * s.maxRange;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float lineH = ImGui::GetTextLineHeight();

    if (elems & ovl::HeatmapCorner) {
        g_heat.update(g_snap, dt, g_heatSettings);
        const ImVec2 org(16.0f, 16.0f);
        const int   cols  = g_heat.gridCols(g_heatSettings);
        const int   rows  = g_heat.gridRows(g_heatSettings);
        const float step  = g_heatSettings.cellSize + g_heatSettings.gap;
        const float gridW = cols * step;
        const float gridH = rows * step;

        char cnt[96];
        std::snprintf(cnt, sizeof(cnt), "Objects: %d   Players: %d   Slots: %d",
                      g_heat.liveObjects(), g_heat.livePlayers(), g_snap.maxEntries);
        const ImVec2 cntSz  = ImGui::CalcTextSize(cnt);
        const float  panelW = (gridW > cntSz.x) ? gridW : cntSz.x;

        dl->AddRectFilled(ImVec2(org.x - 4, org.y - 4),
                          ImVec2(org.x + panelW + 4, org.y + gridH + 4 + lineH + 2),
                          IM_COL32(0, 0, 0, 150), 3.0f);
        g_heat.drawOverlay(dl, org, g_heatSettings);
        dl->AddText(ImVec2(org.x, org.y + gridH + 4), IM_COL32(210, 215, 225, 255), cnt);
    }

    CameraData cam;
    if (!g_reader.cameraData(cam)) return;
    const float vfov = (cam.fovV > 0.0001f) ? cam.fovV
                                            : overlay::horizToVertFov(cam.fovH, vw, vh);
    float vp[16];
    overlay::buildViewProj(cam.pos, cam.fwd, cam.up, vfov, vw, vh, vp);

    for (int i = 0; i < g_snap.maxEntries; ++i) {
        if (!g_snap.occ[i] || !g_snap.objAddr[i]) continue;

        const uint8_t cat = g_snap.cat[i];
        if (cat < 32 && !(s.types & (1u << cat))) continue;

        ObjectDetail d;
        if (needTag) {
            if (!g_reader.objectDetail(g_snap.objAddr[i], d) || !d.hasPos) continue;
        } else {
            if (!g_reader.objectPosition(g_snap.objAddr[i], d.pos)) continue;
        }

        const float ddx = d.pos[0] - cam.pos[0];
        const float ddy = d.pos[1] - cam.pos[1];
        const float ddz = d.pos[2] - cam.pos[2];
        const float dist2 = ddx*ddx + ddy*ddy + ddz*ddz;
        if (dist2 < minR2 || dist2 > maxR2) continue;

        float sx, sy;
        if (!overlay::worldToScreen(vp, d.pos, vw, vh, sx, sy)) continue;
        if (sx < 0 || sy < 0 || sx > vw || sy > vh) continue;

        const int a = (dist2 < (1.5f * 1.5f)) ? 64 : 255;

        const CatInfo& c = gTypes.cat(cat);
        const ImU32 catCol = IM_COL32(c.r | 1, c.g, c.b, a);

        if (elems & ovl::Marker)
            dl->AddCircleFilled(ImVec2(sx, sy), 3.0f, catCol);

        const int pidx = g_snap.playerIndex[i];
        const char* gamertag = nullptr;
        const char* service  = nullptr;
        if (pidx >= 0) {
            if (pidx < (int)g_snap.playerNames.size() && !g_snap.playerNames[pidx].empty())
                gamertag = g_snap.playerNames[pidx].c_str();
            if (pidx < (int)g_snap.playerServices.size() && !g_snap.playerServices[pidx].empty())
                service = g_snap.playerServices[pidx].c_str();
        }

        float ty = sy - 6.0f;
        auto line = [&](ImU32 color, const char* text) {
            dl->AddText(ImVec2(sx + 5, ty), color, text);
            ty += lineH;
        };

        if ((elems & ovl::Gamertag) && gamertag) line(IM_COL32(255, 196, 42, a), gamertag);
        if ((elems & ovl::ServiceTag) && service) line(IM_COL32(255, 196, 42, a), service);
        if (elems & ovl::TypeName)               line(catCol, c.name.c_str());
        if ((elems & ovl::TagName) && d.hasTag) {
            const char* shortName = std::strrchr(d.tag, '\\');
            line(IM_COL32(230, 230, 235, a), shortName ? shortName + 1 : d.tag);
        }
        if ((elems & ovl::TagPath) && d.hasTag)  line(IM_COL32(200, 200, 205, a), d.tag);
        if (needAttach) {
            Attachment att;
            if (g_reader.attachmentInfo(g_snap, d, att)) {
                if (!att.parent.empty()) {
                    std::string t = "Parent: " + att.parent;
                    line(IM_COL32(200, 174, 255, a), t.c_str());
                }
                if (!att.children.empty()) {
                    std::string t = "Attached: " + att.children;
                    line(IM_COL32(200, 174, 255, a), t.c_str());
                }
            }
        }
        if (elems & ovl::Coords) {
            char xyz[64];
            std::snprintf(xyz, sizeof(xyz), "%.1f, %.1f, %.1f", d.pos[0], d.pos[1], d.pos[2]);
            line(IM_COL32(170, 195, 220, a), xyz);
        }
        if (elems & ovl::Datum) {
            const uint16_t salt = uint16_t(g_snap.salt[i]);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "datum %08X", (uint32_t(salt) << 16) | uint16_t(i));
            line(IM_COL32(230, 230, 235, a), buf);
        }
        if (elems & ovl::Salt) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "salt %04X", uint16_t(g_snap.salt[i]));
            line(IM_COL32(230, 230, 235, a), buf);
        }
    }
}

static DWORD WINAPI unloadThread(LPVOID) {
    Sleep(150);
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    Sleep(50);
    FreeLibraryAndExitThread(g_self, 0);
    return 0;
}

static void __stdcall hkExec(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists) {
    if (q && g12_candN < 8) {
        const D3D12_COMMAND_QUEUE_DESC qd = q->GetDesc();
        if (qd.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            bool seen = false;
            const LONG have = g12_candN;
            for (LONG i = 0; i < have && i < 8; ++i)
                if (g12_cands[i] == q) { seen = true; break; }
            if (!seen) {
                const LONG slot = InterlockedIncrement(&g12_candN) - 1;
                if (slot < 8) g12_cands[slot] = q;
                else          InterlockedDecrement(&g12_candN);
            }
        }
    }
    oExec(q, n, lists);
}

static HRESULT __stdcall hkPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (g_cleaned) return oPresent(sc, sync, flags);

    if (!g_init) {
        if (InterlockedCompareExchange(&g_initGuard, 1, 0) != 0)
            return oPresent(sc, sync, flags);

        DXGI_SWAP_CHAIN_DESC d;
        if (FAILED(sc->GetDesc(&d))) { InterlockedExchange(&g_initGuard, 0); return oPresent(sc, sync, flags); }

        Backend want = Backend::Unknown;
        ID3D12Device* probe = nullptr;
        if (SUCCEEDED(sc->GetDevice(IID_PPV_ARGS(&probe))) && probe) {
            probe->Release();
            want = Backend::D3D12;
        } else if (SUCCEEDED(sc->GetDevice(IID_PPV_ARGS(&g_dev))) && g_dev) {
            want = Backend::D3D11;
        } else {
            InterlockedExchange(&g_initGuard, 0);
            return oPresent(sc, sync, flags);
        }
        if (want == Backend::D3D12 && g12_candN == 0) {
            if (g_dev) { g_dev->Release(); g_dev = nullptr; }
            InterlockedExchange(&g_initGuard, 0);
            return oPresent(sc, sync, flags);
        }

        g_hwnd = d.OutputWindow;
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(g_hwnd);

        bool ok = false;
        if (want == Backend::D3D12) {
            ok = init12(sc, d);
        } else {
            g_dev->GetImmediateContext(&g_ctx);
            createRTV(sc);
            ok = ImGui_ImplDX11_Init(g_dev, g_ctx);
        }
        if (!ok) {
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            shutdown12();
            if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
            if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
            if (g_dev) { g_dev->Release(); g_dev = nullptr; }
            InterlockedExchange(&g_initGuard, 0);
            return oPresent(sc, sync, flags);
        }

        g_backend = want;
        g_reader.loadConfig(dllDir() + "config.xml");
        std::string ts;
        gTypes.load(dllDir() + "types.xml", ts);
        g_init = true;
    }

    if (g_backend == Backend::D3D12 && sc != g12_sc) return oPresent(sc, sync, flags);

    if (g_unloadEvent && WaitForSingleObject(g_unloadEvent, 0) == WAIT_OBJECT_0) {
        if (g_backend == Backend::D3D12) ImGui_ImplDX12_Shutdown();
        else                             ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
        shutdown12();
        g_cleaned = true;
        CreateThread(nullptr, 0, unloadThread, nullptr, 0, nullptr);
        return oPresent(sc, sync, flags);
    }

    if (!g_attached) g_attached = g_reader.attachSelf();

    DXGI_SWAP_CHAIN_DESC d;
    if (FAILED(sc->GetDesc(&d))) return oPresent(sc, sync, flags);
    const float vw = float(d.BufferDesc.Width);
    const float vh = float(d.BufferDesc.Height);

    if (g_backend == Backend::D3D12) ImGui_ImplDX12_NewFrame();
    else                             ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::GetIO().DisplaySize = ImVec2(vw, vh);
    ImGui::NewFrame();
    drawMarkers(vw, vh);
    ImGui::Render();

    if (g_backend == Backend::D3D12) {
        render12(sc);
    } else {
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    return oPresent(sc, sync, flags);
}

static HRESULT __stdcall hkResize(IDXGISwapChain* sc, UINT bc, UINT w, UINT h,
                                  DXGI_FORMAT fmt, UINT flags) {
    const bool ours = (g_backend != Backend::D3D12) || (sc == g12_sc);

    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (ours) releaseTargets12();

    HRESULT hr = oResize(sc, bc, w, h, fmt, flags);

    if (g_init && ours) {
        if (g_backend == Backend::D3D12) {
            DXGI_SWAP_CHAIN_DESC nd;
            const bool haveDesc = SUCCEEDED(sc->GetDesc(&nd));
            const UINT newCount = haveDesc ? nd.BufferCount : g12_count;
            const DXGI_FORMAT newFmt = haveDesc ? nd.BufferDesc.Format : g12_fmt;
            if (newCount != g12_count || newFmt != g12_fmt) {
                ImGui_ImplDX12_Shutdown();
                shutdown12();
                g_init = false;
                g_backend = Backend::Unknown;
                return hr;
            }
            createTargets12(sc);
        } else {
            createRTV(sc);
        }
    }
    return hr;
}

static DWORD WINAPI initThread(LPVOID) {
    WNDCLASSEXA wc = { sizeof(wc), 0, DefWindowProcA, 0, 0,
                       GetModuleHandleA(nullptr), 0, 0, 0, 0, "phosphor_dummy", 0 };
    RegisterClassExA(&wc);
    HWND dummy = CreateWindowA("phosphor_dummy", "dummy", WS_OVERLAPPEDWINDOW,
                               0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = dummy;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL lv[] = { D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        lv, 1, D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
    if (FAILED(hr)) {
        DestroyWindow(dummy);
        UnregisterClassA("phosphor_dummy", wc.hInstance);
        return 0;
    }

    void** vtbl = *reinterpret_cast<void***>(sc);
    void* presentAddr = vtbl[8];
    void* resizeAddr  = vtbl[13];
    sc->Release(); dev->Release(); ctx->Release();
    DestroyWindow(dummy);
    UnregisterClassA("phosphor_dummy", wc.hInstance);

    void* execAddr = nullptr;
    if (HMODULE d12 = GetModuleHandleA("d3d12.dll")) {
        auto createDevice =
            reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(d12, "D3D12CreateDevice"));
        ID3D12Device* dev12 = nullptr;
        if (createDevice &&
            SUCCEEDED(createDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev12))) &&
            dev12) {
            D3D12_COMMAND_QUEUE_DESC qd = {};
            qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            ID3D12CommandQueue* q = nullptr;
            if (SUCCEEDED(dev12->CreateCommandQueue(&qd, IID_PPV_ARGS(&q))) && q) {
                void** qv = *reinterpret_cast<void***>(q);
                execAddr = qv[10];
                q->Release();
            }
            dev12->Release();
        }
    }

    g_unloadEvent = CreateEventW(nullptr, FALSE, FALSE, ovl::kUnloadEvent);

    HANDLE smap = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, ovl::kSettingsMapping);
    if (!smap)
        smap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  sizeof(ovl::Settings), ovl::kSettingsMapping);
    if (smap) {
        g_settings = reinterpret_cast<ovl::Settings*>(
            MapViewOfFile(smap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ovl::Settings)));
        if (g_settings && g_settings->magic != ovl::kMagic)
            ovl::initDefaults(*g_settings);
    }

    if (MH_Initialize() != MH_OK) return 0;
    MH_CreateHook(presentAddr, &hkPresent, reinterpret_cast<void**>(&oPresent));
    MH_CreateHook(resizeAddr,  &hkResize,  reinterpret_cast<void**>(&oResize));
    if (execAddr) MH_CreateHook(execAddr, &hkExec, reinterpret_cast<void**>(&oExec));
    MH_EnableHook(MH_ALL_HOOKS);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        CreateThread(nullptr, 0, initThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
