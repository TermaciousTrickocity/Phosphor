#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <tchar.h>
#include <tlhelp32.h>

#include <string>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "mcc.hpp"
#include "heatmap.hpp"
#include "overlay.hpp"
#include "overlay_shared.hpp"

static ovl::Settings* g_ovlSettings = nullptr;

static ID3D11Device*           g_pd3dDevice        = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext = nullptr;
static IDXGISwapChain1*        g_pSwapChain        = nullptr;
static ID3D11RenderTargetView* g_mainRTV           = nullptr;
static IDCompositionDevice*    g_dcompDevice       = nullptr;
static IDCompositionTarget*    g_dcompTarget       = nullptr;
static IDCompositionVisual*    g_dcompVisual       = nullptr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static void CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_mainRTV);
        backBuffer->Release();
    }
}

static void CleanupRenderTarget() {
    if (g_mainRTV) { g_mainRTV->Release(); g_mainRTV = nullptr; }
}

static bool CreateDeviceD3D(HWND hWnd) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
            D3D11_SDK_VERSION, &g_pd3dDevice, &fl, &g_pd3dDeviceContext) != S_OK)
        return false;

    IDXGIDevice* dxgiDevice = nullptr;
    if (g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice)) != S_OK) return false;
    IDXGIAdapter* adapter = nullptr;
    dxgiDevice->GetAdapter(&adapter);
    IDXGIFactory2* factory = nullptr;
    adapter->GetParent(IID_PPV_ARGS(&factory));

    RECT rc; GetClientRect(hWnd, &rc);
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width       = std::max<LONG>(1, rc.right - rc.left);
    sd.Height      = std::max<LONG>(1, rc.bottom - rc.top);
    sd.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode   = DXGI_ALPHA_MODE_PREMULTIPLIED;
    sd.Scaling     = DXGI_SCALING_STRETCH;

    bool ok = factory->CreateSwapChainForComposition(g_pd3dDevice, &sd, nullptr, &g_pSwapChain) == S_OK
        && DCompositionCreateDevice(dxgiDevice, IID_PPV_ARGS(&g_dcompDevice)) == S_OK
        && g_dcompDevice->CreateTargetForHwnd(hWnd, TRUE, &g_dcompTarget) == S_OK
        && g_dcompDevice->CreateVisual(&g_dcompVisual) == S_OK;
    if (ok) {
        g_dcompVisual->SetContent(g_pSwapChain);
        g_dcompTarget->SetRoot(g_dcompVisual);
        g_dcompDevice->Commit();
    }

    if (factory)    factory->Release();
    if (adapter)    adapter->Release();
    if (dxgiDevice) dxgiDevice->Release();
    if (!ok) return false;

    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_dcompVisual) { g_dcompVisual->Release(); g_dcompVisual = nullptr; }
    if (g_dcompTarget) { g_dcompTarget->Release(); g_dcompTarget = nullptr; }
    if (g_dcompDevice) { g_dcompDevice->Release(); g_dcompDevice = nullptr; }
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void DrawObjectTable(MccReader& reader, const PoolSnapshot& snap,
                            int selectedSlot, int& outHover, int& outClick) {
    if (!reader.attached() || !snap.valid) {
        ImGui::TextDisabled("Not attached to a game.");
        return;
    }

    const ImVec4 kGreen(0.35f, 1.0f, 0.45f, 1.0f);
    const ImVec4 kOrange(1.0f, 0.65f, 0.2f, 1.0f);
    const ImVec4 kRed(1.0f, 0.35f, 0.35f, 1.0f);
    const ImVec4 kWhite(0.9f, 0.9f, 0.9f, 1.0f);

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

    if (!ImGui::BeginTable("ObjectsTable", 7, flags)) return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Datum",       ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableSetupColumn("Index",       ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableSetupColumn("Salt",        ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableSetupColumn("Player",      ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("Coordinates", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Type",        ImGuiTableColumnFlags_WidthFixed, 96.0f);
    ImGui::TableSetupColumn("Tag Name",    ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(snap.maxEntries);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            ImGui::TableNextRow();
            const bool occ = i < (int)snap.occ.size() && snap.occ[i];
            const bool isFree = (i == snap.nextIndex);

            ImGui::TableSetColumnIndex(0);
            char rid[16];
            std::snprintf(rid, sizeof(rid), "##row%d", i);
            if (ImGui::Selectable(rid, selectedSlot == i,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                outClick = i;
            if (ImGui::IsItemHovered()) outHover = i;

            if (occ) {
                const uint16_t salt = uint16_t(snap.salt[i]);
                const uint32_t datum = (uint32_t(salt) << 16) | uint16_t(i);

                ImGui::SameLine();
                ImGui::TextColored(isFree ? kOrange : kGreen, "%08X", datum);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(isFree ? kOrange : kGreen, "%04X", i);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(kWhite, "%04X", salt);

                ImGui::TableSetColumnIndex(3);
                if (snap.playerIndex[i] >= 0)
                    ImGui::TextColored(kGreen, "%d", snap.playerIndex[i]);

                ObjectDetail d;
                const bool haveDetail = snap.objAddr[i] &&
                    reader.objectDetail(snap.objAddr[i], d);

                ImGui::TableSetColumnIndex(4);
                if (haveDetail && d.hasPos)
                    ImGui::TextColored(kWhite, "%+.2f %+.2f %+.2f", d.pos[0], d.pos[1], d.pos[2]);

                ImGui::TableSetColumnIndex(5);
                ImGui::TextColored(kWhite, "%s", gTypes.cat(snap.cat[i]).name.c_str());

                ImGui::TableSetColumnIndex(6);
                if (haveDetail && d.hasTag) {
                    const char* shortName = std::strrchr(d.tag, '\\');
                    ImGui::TextColored(kWhite, "%s", shortName ? shortName + 1 : d.tag);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.tag);
                } else if (haveDetail) {
                    ImGui::TextColored(kWhite, "%08X", d.tagDatum);
                }
            } else {
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(isFree ? kOrange : kRed, "%04X", i);
            }
        }
    }

    ImGui::EndTable();
}

static void DrawLegend(uint32_t mask) {
    const float spacing = 14.0f;
    const float maxX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    bool first = true;
    for (int c = 0; c < gTypes.count() && c < 32; ++c) {
        if (!(mask & (1u << c))) continue;
        const CatInfo& ci = gTypes.cat(c);
        if (!first) {
            const float itemW = 14 + 4 + ImGui::CalcTextSize(ci.name.c_str()).x + spacing;
            if (ImGui::GetItemRectMax().x + itemW < maxX)
                ImGui::SameLine(0, spacing);
        }
        ImVec4 col(ci.r / 255.0f, ci.g / 255.0f, ci.b / 255.0f, 1.0f);
        ImGui::ColorButton(ci.name.c_str(), col,
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop |
            ImGuiColorEditFlags_NoBorder, ImVec2(14, 14));
        ImGui::SameLine(0, 4);
        ImGui::TextUnformatted(ci.name.c_str());
        first = false;
    }
    if (first) ImGui::TextDisabled("(no objects yet)");
}

static void FormatDuration(float sec, char* out, size_t n) {
    const int t = (sec > 0.0f) ? int(sec) : 0;
    if      (sec < 60.0f)   std::snprintf(out, n, "%.1f s", sec < 0.0f ? 0.0f : sec);
    else if (sec < 3600.0f) std::snprintf(out, n, "%dm %02ds", t / 60, t % 60);
    else                    std::snprintf(out, n, "%dh %02dm", t / 3600, (t / 60) % 60);
}

static void DrawInfoPane(MccReader& reader, const PoolSnapshot& snap,
                         const Heatmap& hm, int slot) {
    ImGui::BeginChild("infopane", ImVec2(0, 208), true);

    const bool haveSlot = snap.valid && slot >= 0 && slot < snap.maxEntries;
    if (!haveSlot) {
        ImGui::TextDisabled("Hover or click a slot in the heatmap or object table to inspect it.");
        ImGui::EndChild();
        return;
    }

    const float paneW  = ImGui::GetContentRegionAvail().x;
    const float rightW = std::max(340.0f, paneW - 380.0f - 16.0f);

    const ImVec4 dim(0.60f, 0.62f, 0.68f, 1.0f);
    const ImVec4 val(0.92f, 0.94f, 0.98f, 1.0f);
    const ImVec4 addrCol(0.45f, 0.58f, 0.78f, 1.0f);

    auto row = [&](const char* label, const ImVec4& color, uint64_t addr, const char* fmt, ...) {
        va_list ap; va_start(ap, fmt);
        char buf[300]; std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (addr) ImGui::TextColored(addrCol, "[%llX]", (unsigned long long)addr);
        ImGui::TableSetColumnIndex(1); ImGui::TextColored(dim, "%s", label);
        ImGui::TableSetColumnIndex(2); ImGui::TextColored(color, "%s", buf);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", buf);
    };

    const bool     occ   = slot < (int)snap.occ.size() && snap.occ[slot];
    const uint16_t salt  = uint16_t(snap.salt[slot]);
    const uint32_t datum = (uint32_t(salt) << 16) | uint16_t(slot);
    const uint8_t  cat   = snap.cat[slot];
    const int16_t  pidx  = snap.playerIndex[slot];
    const uint64_t addr  = snap.objAddr[slot];
    const float    inten = hm.intensityAt(slot);
    const char*    state = occ ? "occupied" : (inten > 0.0f ? "decaying (vacated)" : "empty");

    const GameConfig* g = reader.game();
    const uint64_t entryAddr = snap.objDataStart ? snap.objDataStart + uint64_t(slot) * snap.objDataSizeof : 0;
    const uint64_t typeAddr  = (g && entryAddr) ? entryAddr + g->objTypeOff : 0;
    const uint64_t plrAddr   = (snap.plrDataStart && pidx >= 0) ? snap.plrDataStart + uint64_t(pidx) * snap.plrDataSizeof : 0;
    const uint64_t nameAddr  = (g && plrAddr && g->playerNameLen)    ? plrAddr + g->playerNameOff    : 0;
    const uint64_t svcAddr   = (g && plrAddr && g->playerServiceLen) ? plrAddr + g->playerServiceOff : 0;

    ObjectDetail d;
    const bool haveDetail = occ && addr && reader.objectDetail(addr, d);

    const ImGuiTableFlags tflags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

    if (ImGui::BeginTable("infoL", 3, tflags, ImVec2(380, 0))) {
        ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthFixed, 175);
        row("Slot",  val, entryAddr, "0x%04X (%d)", slot, slot);
        row("Datum", val, 0,         "%08X", datum);
        row("Salt",  val, entryAddr, "0x%04X", salt);
        row("State", occ ? ImVec4(0.4f,1,0.5f,1) : dim, 0, "%s", state);

        const float held   = hm.filledFor(slot);
        const float vacant = hm.vacantFor(slot);
        char dur[32];
        if (held >= 0.0f) {
            FormatDuration(held, dur, sizeof(dur));
            row("Occupied", val, 0, "%s%s", hm.ageExact(slot) ? "" : ">= ", dur);
        } else if (vacant >= 0.0f) {
            FormatDuration(vacant, dur, sizeof(dur));
            row("Unoccupied", dim, 0, "%s ago", dur);
        } else {
            row("Occupied", dim, 0, "N/A");
        }

        const CatInfo& ci = gTypes.cat(cat);
        row("Type",  ImVec4(ci.r/255.f, ci.g/255.f, ci.b/255.f, 1.f), typeAddr,
                     "%s (0x%02X)", ci.name.c_str(), snap.typeByte[slot]);
        ImGui::EndTable();
    }

    ImGui::SameLine(0, 16);

    if (ImGui::BeginTable("infoR", 3, ImGuiTableFlags_SizingFixedFit, ImVec2(rightW, 0))) {
        ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 118);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
        if (pidx >= 0) {
            row("Player", ImVec4(1,0.77f,0.16f,1), plrAddr, "index %d", pidx);
            const bool haveName = pidx < (int)snap.playerNames.size() && !snap.playerNames[pidx].empty();
            if (haveName)
                row("Gamertag", ImVec4(1,0.77f,0.16f,1), nameAddr, "\"%s\"", snap.playerNames[pidx].c_str());
            const bool haveSvc = pidx < (int)snap.playerServices.size() && !snap.playerServices[pidx].empty();
            if (haveSvc)
                row("Servicetag", ImVec4(1,0.77f,0.16f,1), svcAddr, "\"%s\"", snap.playerServices[pidx].c_str());
        } else {
            row("Player", dim, 0, "-");
        }

        if (haveDetail && d.hasTag) {
            const char* shortName = std::strrchr(d.tag, '\\');
            row("Tag", val, d.tagNameAddr, "%s", shortName ? shortName + 1 : d.tag);
            row("Tag path", dim, 0, "%s", d.tag);
        } else if (haveDetail) {
            row("Tag", dim, d.tagDatumAddr, "%08X (unresolved)", d.tagDatum);
        } else {
            row("Tag", dim, 0, "-");
        }
        if (haveDetail) row("Tag datum", val, d.tagDatumAddr, "%08X (index 0x%04X)", d.tagDatum, d.tagDatum & 0xFFFF);

        if (haveDetail && d.hasPos) row("Position", val, d.posAddr, "%+.3f %+.3f %+.3f", d.pos[0], d.pos[1], d.pos[2]);
        else                        row("Position", dim, 0, "-");

        Attachment att;
        if (haveDetail && reader.attachmentInfo(snap, d, att)) {
            const ImVec4 rel(0.78f, 0.68f, 1.0f, 1.0f);
            if (!att.parent.empty()) {
                row("Parent",      rel, d.parentDatumAddr, "%s", att.parent.c_str());
                if (!att.parentPath.empty()) row("Parent path", dim, 0, "%s", att.parentPath.c_str());
            }
            if (!att.children.empty()) {
                row("Attachments", rel, d.childDatumAddr, "%s", att.children.c_str());
                if (!att.childrenPaths.empty()) row("Attachment paths", dim, 0, "%s", att.childrenPaths.c_str());
            }
        }
        ImGui::EndTable();
    }

    ImGui::EndChild();
}

static void UpdateFrameData(MccReader& reader, Heatmap& heatmap,
                            HeatmapSettings& settings, PoolSnapshot& lastSnap, float dt) {
    static std::chrono::steady_clock::time_point lastTry;
    const auto now = std::chrono::steady_clock::now();

    auto clearLive = [](PoolSnapshot& s) {
        std::fill(s.occ.begin(), s.occ.end(), (uint8_t)0);
        std::fill(s.isPlayer.begin(), s.isPlayer.end(), (uint8_t)0);
        std::fill(s.playerIndex.begin(), s.playerIndex.end(), (int16_t)-1);
        std::fill(s.objAddr.begin(), s.objAddr.end(), (uint64_t)0);
        s.nextIndex = -1;
        s.objectCount = 0;
        s.playerCount = 0;
    };

    if (!reader.attached()) {
        if (std::chrono::duration<float>(now - lastTry).count() >= 1.0f) {
            lastTry = now;
            reader.tryAttach();
        }
        clearLive(lastSnap);
    } else {
        PoolSnapshot snap;
        if (reader.sample(snap)) {
            lastSnap = snap;
        } else {
            reader.detach();
            clearLive(lastSnap);
        }
    }

    heatmap.update(lastSnap, dt, settings);
}

static std::string InjectOverlayDll(uint32_t pid, const char* gameName) {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string dir(exePath);
    const size_t slash = dir.find_last_of("\\/");
    if (slash != std::string::npos) dir.resize(slash + 1);
    const std::string dll = dir + "memory.dll";
    if (GetFileAttributesA(dll.c_str()) == INVALID_FILE_ATTRIBUTES)
        return "memory.dll not found next to the exe";

    if (!pid) return "not attached to a game — nothing to inject into";

    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc) return "OpenProcess failed (try running as admin)";

    const size_t len = dll.size() + 1;
    void* remote = VirtualAllocEx(proc, nullptr, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    std::string result;
    if (!remote) {
        result = "VirtualAllocEx failed";
    } else {
        WriteProcessMemory(proc, remote, dll.c_str(), len, nullptr);
        auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"));
        HANDLE thread = CreateRemoteThread(proc, nullptr, 0, loadLib, remote, 0, nullptr);
        if (!thread) {
            result = "CreateRemoteThread failed";
        } else {
            WaitForSingleObject(thread, 5000);
            CloseHandle(thread);
            result = "Injected memory.dll into " + std::string(gameName) +
                     " (pid " + std::to_string(pid) + ")";
        }
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    }
    CloseHandle(proc);
    return result;
}

static std::string UnloadOverlayDll() {
    HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, ovl::kUnloadEvent);
    if (!ev) return "overlay not injected (nothing to unload)";
    SetEvent(ev);
    CloseHandle(ev);
    return "Requested overlay unload";
}

static void DrawUI(MccReader& reader, Heatmap& heatmap, HeatmapSettings& settings,
                   PoolSnapshot& lastSnap) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Phosphor", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_MenuBar);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit")) PostQuitMessage(0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Settings")) {
            ImGui::SetNextItemWidth(210);
            ImGui::SliderInt("Columns", &settings.columns, 8, 128);
            ImGui::SetNextItemWidth(210);
            ImGui::SliderFloat("Cell size", &settings.cellSize, 4.0f, 28.0f, "%.0f px");
            ImGui::SetNextItemWidth(210);
            ImGui::SliderFloat("Decay half-life", &settings.halfLife, 0.01f, 5.0f, "%.2f s");
            ImGui::Checkbox("Pause (freeze decay & sampling)", &settings.paused);

            ImGui::Separator();
            ImGui::TextDisabled("Pool maximum (debug)");
            ImGui::TextDisabled("Reported by game: %d", reader.headerMaxEntries());
            bool ovr = reader.maxEntriesOverride() > 0;
            if (ImGui::Checkbox("Override maximum slots", &ovr)) {
                const int hdr = reader.headerMaxEntries();
                reader.setMaxEntriesOverride(ovr ? (hdr > 0 ? hdr : 2048) : 0);
            }
            if (reader.maxEntriesOverride() > 0) {
                int v = reader.maxEntriesOverride();
                ImGui::SetNextItemWidth(210);
                if (ImGui::SliderInt("Maximum slots", &v, 1, 65536)) reader.setMaxEntriesOverride(v);
                ImGui::SetNextItemWidth(210);
                if (ImGui::InputInt("Exact", &v)) reader.setMaxEntriesOverride(v);
                ImGui::TextDisabled("extends past the real pool into adjacent memory");
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Overlay")) {
            static std::string injectStatus;
            const bool live = reader.attached();
            if (ImGui::MenuItem("Inject in-game overlay (memory.dll)", nullptr, false, live))
                injectStatus = InjectOverlayDll(reader.pid(), reader.game()->name.c_str());
            if (ImGui::MenuItem("Unload in-game overlay"))
                injectStatus = UnloadOverlayDll();
            ImGui::TextDisabled("Injects/ejects the overlay DLL in the attached game");
            ImGui::TextDisabled("(renders inside the game; D3D11 and D3D12 both supported).");
            if (!live)
                ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "Attach to a game first.");
            if (!injectStatus.empty()) ImGui::TextColored(ImVec4(0.4f, 1, 0.5f, 1), "%s", injectStatus.c_str());

            ImGui::Separator();
            if (ImGui::BeginMenu("Elements to show")) {
                if (g_ovlSettings) {
                    auto toggle = [&](const char* label, uint32_t bit) {
                        bool on = (g_ovlSettings->elements & bit) != 0;
                        if (ImGui::MenuItem(label, nullptr, on))
                            g_ovlSettings->elements ^= bit;
                    };
                    toggle("Marker dot",  ovl::Marker);
                    toggle("Type",        ovl::TypeName);
                    toggle("Gamertag",    ovl::Gamertag);
                    toggle("Service tag", ovl::ServiceTag);
                    toggle("Tag name",    ovl::TagName);
                    toggle("Tag path",    ovl::TagPath);
                    toggle("Attachment",  ovl::Attachment);
                    toggle("Coordinates", ovl::Coords);
                    toggle("Datum",       ovl::Datum);
                    toggle("Salt",        ovl::Salt);
                    ImGui::Separator();
                    toggle("Heatmap (corner)", ovl::HeatmapCorner);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Types to show")) {
                if (g_ovlSettings) {
                    if (ImGui::MenuItem("All"))  g_ovlSettings->types = 0xFFFFFFFFu;
                    if (ImGui::MenuItem("None")) g_ovlSettings->types = 0u;
                    ImGui::Separator();
                    for (int c = 0; c < gTypes.count() && c < 32; ++c) {
                        bool on = (g_ovlSettings->types & (1u << c)) != 0;
                        if (ImGui::MenuItem(gTypes.cat(c).name.c_str(), nullptr, on))
                            g_ovlSettings->types ^= (1u << c);
                    }
                }
                ImGui::EndMenu();
            }

            if (g_ovlSettings) {
                ImGui::Separator();
                ImGui::TextDisabled("Visibility range (world units)");
                ImGui::SetNextItemWidth(200);
                ImGui::DragFloat("Minimum", &g_ovlSettings->minRange, 1.0f, 0.0f, 100000.0f, "%.0f");
                ImGui::SetNextItemWidth(200);
                ImGui::DragFloat("Maximum", &g_ovlSettings->maxRange, 1.0f, 0.0f, 100000.0f, "%.0f");

                ImGui::Separator();
                ImGui::TextDisabled("Corner heatmap size");
                ImGui::SetNextItemWidth(200);
                ImGui::SliderFloat("Cell size", &g_ovlSettings->heatCell, 1.0f, 12.0f, "%.0f px");
                ImGui::SetNextItemWidth(200);
                ImGui::SliderInt("Columns", &g_ovlSettings->heatCols, 8, 128);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Info")) {
            ImGui::TextDisabled("Phosphor - read-only Blam object pool heatmap");
            ImGui::TextDisabled("Config: %s", reader.configStatus().c_str());
            ImGui::TextDisabled("Types:  %s", gTypes.status().c_str());
            ImGui::Separator();
            ImGui::TextUnformatted("Object type legend:");
            const int nc = gTypes.count();
            DrawLegend(nc >= 32 ? 0xFFFFFFFFu : ((1u << nc) - 1u));
            ImGui::Separator();
            ImGui::TextDisabled("* + cycling color + white border = player-controlled");
            ImGui::TextDisabled("white outline = next free slot");
            ImGui::TextDisabled("cyan outline = selected slot");
            ImGui::TextDisabled("green outline = parent of focused object");
            ImGui::TextDisabled("orange outline = attachment of focused object");
            ImGui::TextDisabled("fading cell = recently vacated (decaying)");
            ImGui::EndMenu();
        }

        ImGui::TextDisabled("     ");
        if (!reader.attached()) {
            ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "Searching for a game...");
        } else {
            ImGui::TextColored(ImVec4(0.4f, 1, 0.5f, 1), "%s", reader.game()->name.c_str());
            ImGui::TextDisabled("(process %u)", reader.pid());
            ImGui::Text("Capacity: %d/%d", lastSnap.capacity, lastSnap.maxEntries);
            ImGui::TextColored(ImVec4(0.4f, 1, 0.5f, 1), "Objects: %d", heatmap.liveObjects());
            ImGui::TextColored(ImVec4(1, 0.77f, 0.16f, 1), "Players: %d", heatmap.livePlayers());
        }
        ImGui::EndMenuBar();
    }

    static int selectedSlot = -1;
    static int hoverSlot    = -1;
    const int paneSlot = (selectedSlot >= 0) ? selectedSlot : hoverSlot;
    DrawInfoPane(reader, lastSnap, heatmap, paneSlot);

    int parentSlot = -1;
    std::vector<int> childSlots;
    if (reader.attached() && lastSnap.valid && paneSlot >= 0 && paneSlot < lastSnap.maxEntries &&
        paneSlot < (int)lastSnap.occ.size() && lastSnap.occ[paneSlot] && lastSnap.objAddr[paneSlot]) {
        ObjectDetail d;
        Attachment att;
        if (reader.objectDetail(lastSnap.objAddr[paneSlot], d) &&
            reader.attachmentInfo(lastSnap, d, att)) {
            parentSlot = att.parentSlot;
            childSlots = std::move(att.childSlots);
        }
    }

    int newHover = -1, newClick = -1;
    if (ImGui::BeginTabBar("views")) {
        if (ImGui::BeginTabItem("Heatmap")) {
            DrawLegend(heatmap.presentCats());
            ImGui::BeginChild("grid", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            heatmap.draw(settings, selectedSlot, parentSlot, childSlots);
            ImGui::EndChild();
            newHover = heatmap.hoveredSlot();
            if (heatmap.clickedSlot() >= 0) newClick = heatmap.clickedSlot();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Object Table")) {
            DrawObjectTable(reader, lastSnap, selectedSlot, newHover, newClick);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    hoverSlot = newHover;
    if (newClick >= 0) selectedSlot = (newClick == selectedSlot) ? -1 : newClick;

    ImGui::End();
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInst, nullptr, nullptr,
                       nullptr, nullptr, L"Phosphor", nullptr };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,
                              wc.lpszClassName, L"Phosphor",
                              WS_OVERLAPPEDWINDOW, 100, 100, 1280, 860,
                              nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    MccReader      reader;
    Heatmap        heatmap;
    HeatmapSettings settings;
    PoolSnapshot   lastSnap;

    {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string dir(exePath);
        const size_t slash = dir.find_last_of("\\/");
        if (slash != std::string::npos) dir.resize(slash + 1);
        reader.loadConfig(dir + "config.xml");
        std::string typeStatus;
        gTypes.load(dir + "types.xml", typeStatus);
    }

    {
        HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                      sizeof(ovl::Settings), ovl::kSettingsMapping);
        if (m) {
            g_ovlSettings = reinterpret_cast<ovl::Settings*>(
                MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ovl::Settings)));
            if (g_ovlSettings && g_ovlSettings->magic != ovl::kMagic)
                ovl::initDefaults(*g_ovlSettings);
        }
    }

    auto prev = std::chrono::steady_clock::now();

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        const auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - prev).count();
        prev = now;

        UpdateFrameData(reader, heatmap, settings, lastSnap, dt);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        DrawUI(reader, heatmap, settings, lastSnap);

        ImGui::Render();
        const float clearNormal[4] = { 0.06f, 0.07f, 0.09f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRTV, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRTV, clearNormal);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
