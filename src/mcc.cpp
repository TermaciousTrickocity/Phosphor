#include "mcc.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "tinyxml2.h"

static constexpr uint32_t kDataArraySignature = 0x64407440;
static constexpr wchar_t  kMccProcess[]        = L"MCC-Win64-Shipping.exe";
static constexpr wchar_t  kHceProcess[]        = L"HaloCampaignEvolved.exe";

static std::vector<GameConfig> defaultGameConfigs() {
    return {
    { kHceProcess, L"halosimulation_tag_release.dll", "Halo: Campaign Evolved",   PoolAddrMode::TlsPtr,       0x00000020, 0x00000030, 0x50,   0x70, 0x2C, 0x20,   0x44, 0x40, 0x28, false, 0x28,  0xB0,    16,      0x108,  8,      0x04,    "HaloCampaignEvolved", ObjBaseMode::AbsPtr,     0x10,   0x0000,   0x00,        0x20,   0x50,   0x14,   0x10,  0x0C,    TagNameMode::TagInstance,   0x0182D1E8, 0,          0,     "tag instance", 0x10, 0x00, CameraMode::TlsObserver, 0x4E8,    0,       0x154,  0x17C,  0x188,  0x194,  0 , 0x150 , 0x171FF2C, 16.0f/9.0f },
    { kMccProcess, L"haloreach.dll",                  "Halo: Reach",              PoolAddrMode::RwLivePtr,    0x00BA3034, 0x00ABC6B0, 0x50,   0x70, 0x2C, 0x20,   0x44, 0x40, 0x28, false, 0x28,  0xB0,    16,      0xF4,   8,      0x04,    "HaloReach",           ObjBaseMode::AbsPtr,     0x10,   0x0000,   0x00,        0x20,   0,      0x14,   0x10,  0x00,    TagNameMode::PtrArray,      0x01B04A48, 0,          0,     "", 0, 0, CameraMode::ModulePtr, 0x1901ED0,  0x944,   0x00,   0x28,   0x34,   0x6C,   0xC9FB08 , 0 , 0, 0.0f },
    { kMccProcess, L"halo4.dll",                      "Halo 4",                   PoolAddrMode::RwLivePtr,    0x009D25E8, 0x008A7CC4, 0x50,   0x70, 0x2C, 0x20,   0x44, 0x40, 0x28, false, 0x28,  0xB0,    16,      0x00,   0,      0x04,    "Halo4",               ObjBaseMode::AbsPtr,     0x10,   0x0000,   0x08,        0x40,   0,      0x24,   0x20,  0x1C,    TagNameMode::PtrArray,      0x01AC2E38, 0,          0,     "", 0, 0, CameraMode::ModulePtr, 0x1B1E758,  0x9DC,   0x00,   0x28,   0x34,   0x78,   0x10DB008 , 0 , 0, 0.0f },
    { kMccProcess, L"halo3.dll",                      "Halo 3",                   PoolAddrMode::RwLivePtr,    0x007B0BEC, 0x00729E24, 0x48,   0x60, 0x20, 0x24,   0x3C, 0x38, 0x2C, false, 0x28,  0x58,    16,      0x00,   0,      0x03,    "Halo3",               ObjBaseMode::AbsPtr,     0x10,   0x0000,   0x00,        0x1C,   0,      0x10,   0x0C,  0x08,    TagNameMode::StructArray,   0x001AE148, 0,          0,     "", 0, 0, CameraMode::ModulePtr, 0x125CB24,  0x824,   0x00,   0x28,   0x34,   0x40,   0x2D2F6B0 , 0 , 0, 0.0f },
    { kMccProcess, L"halo3odst.dll",                  "Halo 3: ODST",             PoolAddrMode::RwLivePtr,    0x007B77F4, 0x0072DAF4, 0x48,   0x60, 0x20, 0x24,   0x3C, 0x38, 0x2C, false, 0x28,  0x58,    16,      0x00,   0,      0x03,    "Halo3ODST",           ObjBaseMode::AbsPtr,     0x10,   0x0000,   0x00,        0x1C,   0,      0x10,   0x0C,  0x08,    TagNameMode::StructArray,   0x001BFFC8, 0,          0,     "", 0, 0, CameraMode::ModulePtr, 0x12C196C,  0x85C,   0x00,   0x28,   0x34,   0x40,   0x2D735C0 , 0 , 0, 0.0f },
    { kMccProcess, L"halo2.dll",                      "Halo 2",                   PoolAddrMode::DllPtr,       0x018B7398, 0x00E80A28, 0x00,   0x58, 0x20, 0x24,   0x3C, 0x38, 0x2C, false, 0x2C,  0x00,    0,       0x00,   0,      0x03,    "Halo2",               ObjBaseMode::OffsetLoop, 0x08,   0x65D8,  -0x438,      -0x3F8,  0,      0,      0,     0,       TagNameMode::H2StringTable, 0x015E4B68, 0x015E4B78, 0,     "", 0, 0, CameraMode::Module,    0x15F297C,  0,       0x00,   0x20,   0x2C,   0x38,   0x1997714 , 0 , 0, 0.0f },
    { kMccProcess, L"halo1.dll",                      "Halo: CE",                 PoolAddrMode::DllPtrMinus4, 0x01C42248, 0x01C40480, 0x00,   0x3C, 0x24, 0x26,   0x22, 0x30, 0x2C, true,  0x64,  0x04,    12,      0x00,   0,      0x03,    "Halo1",               ObjBaseMode::OffsetMem,  0x08,   0x0000,   0x34,        0x4C,   0,      0,      0,     0,       TagNameMode::H1Rebase,      0x01C34FB0, 0x02EA3410, 0x02D9CE10, "", 0, 0, CameraMode::Module,    0x2D9BDD4,  0,       0x00,   0x20,   0x2C,   0x38,   0x2D9CB08 , 0 , 0, 0.0f },
    };
}

namespace {

uint64_t parseU64(const char* s)  { return s ? strtoull(s, nullptr, 0) : 0; }
int64_t  parseI64(const char* s)  { return s ? strtoll(s, nullptr, 0) : 0; }
bool     parseBool(const char* s) { return s && (strcmp(s, "true") == 0 || strcmp(s, "1") == 0); }

PoolAddrMode parsePoolMode(const char* s) {
    if (s && strcmp(s, "DllPtr") == 0)       return PoolAddrMode::DllPtr;
    if (s && strcmp(s, "DllPtrMinus4") == 0) return PoolAddrMode::DllPtrMinus4;
    if (s && strcmp(s, "TlsPtr") == 0)       return PoolAddrMode::TlsPtr;
    return PoolAddrMode::RwLivePtr;
}
ObjBaseMode parseBaseMode(const char* s) {
    if (s && strcmp(s, "OffsetMem") == 0)  return ObjBaseMode::OffsetMem;
    if (s && strcmp(s, "OffsetLoop") == 0) return ObjBaseMode::OffsetLoop;
    return ObjBaseMode::AbsPtr;
}
TagNameMode parseTagMode(const char* s) {
    if (s && strcmp(s, "StructArray") == 0)   return TagNameMode::StructArray;
    if (s && strcmp(s, "H2StringTable") == 0) return TagNameMode::H2StringTable;
    if (s && strcmp(s, "H1Rebase") == 0)      return TagNameMode::H1Rebase;
    if (s && strcmp(s, "TagInstance") == 0)   return TagNameMode::TagInstance;
    if (s && strcmp(s, "None") == 0)          return TagNameMode::None;
    return TagNameMode::PtrArray;
}
CameraMode parseCameraMode(const char* s) {
    if (s && strcmp(s, "Module") == 0)      return CameraMode::Module;
    if (s && strcmp(s, "ModulePtr") == 0)   return CameraMode::ModulePtr;
    if (s && strcmp(s, "TlsObserver") == 0) return CameraMode::TlsObserver;
    return CameraMode::None;
}
}

bool MccReader::loadConfig(const std::string& path) {
    using namespace tinyxml2;
    XMLDocument doc;
    if (doc.LoadFile(path.c_str()) != XML_SUCCESS) {
        games_ = defaultGameConfigs();
        configStatus_ = "config.xml not found — using built-in defaults";
        return false;
    }

    XMLElement* root = doc.FirstChildElement("mccconfig");
    if (!root) {
        games_ = defaultGameConfigs();
        configStatus_ = "config.xml has no <mccconfig> root — using defaults";
        return false;
    }

    std::vector<GameConfig> games;
    for (XMLElement* e = root->FirstChildElement("game"); e; e = e->NextSiblingElement("game")) {
        GameConfig g{};
        if (const char* nm = e->Attribute("name")) g.name = nm;
        if (const char* dl = e->Attribute("dll")) {
            std::string d = dl;
            g.dll.assign(d.begin(), d.end());
        }
        if (const char* pr = e->Attribute("process")) {
            std::string s = pr;
            g.process.assign(s.begin(), s.end());
        } else {
            g.process = kMccProcess;
        }
        if (XMLElement* p = e->FirstChildElement("pool")) {
            g.mode       = parsePoolMode(p->Attribute("mode"));
            g.objectLoc  = parseU64(p->Attribute("objectLoc"));
            g.playerLoc  = parseU64(p->Attribute("playerLoc"));
            g.livePtrOff = parseU64(p->Attribute("livePtrOff"));
        }
        if (XMLElement* h = e->FirstChildElement("header")) {
            g.hdrSize   = uint32_t(parseU64(h->Attribute("size")));
            g.offMax    = uint32_t(parseU64(h->Attribute("max")));
            g.offSizeof = uint32_t(parseU64(h->Attribute("sizeof")));
            g.offCap    = uint32_t(parseU64(h->Attribute("cap")));
            g.offNext   = uint32_t(parseU64(h->Attribute("next")));
            g.offSig    = uint32_t(parseU64(h->Attribute("sig")));
            g.fields16  = parseBool(h->Attribute("fields16"));
        }
        if (XMLElement* en = e->FirstChildElement("entry")) {
            g.playerCuOff      = uint32_t(parseU64(en->Attribute("playerCuOff")));
            g.playerNameOff    = uint32_t(parseU64(en->Attribute("nameOff")));
            g.playerNameLen    = uint32_t(parseU64(en->Attribute("nameLen")));
            g.playerServiceOff = uint32_t(parseU64(en->Attribute("serviceOff")));
            g.playerServiceLen = uint32_t(parseU64(en->Attribute("serviceLen")));
            g.objTypeOff       = uint32_t(parseU64(en->Attribute("typeOff")));
            if (const char* es = en->Attribute("enumStyle")) g.enumStyle = es;
        }
        if (XMLElement* o = e->FirstChildElement("object")) {
            g.baseMode    = parseBaseMode(o->Attribute("baseMode"));
            g.objPtrOff   = uint32_t(parseU64(o->Attribute("ptrOff")));
            g.loopBaseOff = parseU64(o->Attribute("loopBaseOff"));
            g.tagDatumOff = parseI64(o->Attribute("tagDatumOff"));
            g.posOff      = parseI64(o->Attribute("posOff"));
            g.oriOff      = parseI64(o->Attribute("oriOff"));
            g.parentOff   = parseI64(o->Attribute("parentOff"));
            g.childOff    = parseI64(o->Attribute("childOff"));
            g.siblingOff  = parseI64(o->Attribute("siblingOff"));
        }
        if (XMLElement* t = e->FirstChildElement("tag")) {
            g.tagMode = parseTagMode(t->Attribute("mode"));
            g.tagA    = parseU64(t->Attribute("a"));
            g.tagB    = parseU64(t->Attribute("b"));
            g.tagC    = parseU64(t->Attribute("c"));
            if (const char* pn = t->Attribute("pool")) g.tagPoolName = pn;
            g.tagRecNameOff = uint32_t(parseU64(t->Attribute("recNameOff")));
            g.tagRecSaltOff = uint32_t(parseU64(t->Attribute("recSaltOff")));
        }
        if (XMLElement* c = e->FirstChildElement("camera")) {
            g.cameraMode = parseCameraMode(c->Attribute("mode"));
            g.camOff0    = parseU64(c->Attribute("off0"));
            g.camOff1    = parseU64(c->Attribute("off1"));
            g.camPosOff  = uint32_t(parseU64(c->Attribute("posOff")));
            g.camFwdOff  = uint32_t(parseU64(c->Attribute("fwdOff")));
            g.camUpOff   = uint32_t(parseU64(c->Attribute("upOff")));
            g.camFovOff  = uint32_t(parseU64(c->Attribute("fovOff")));
            g.camVFovOff   = parseU64(c->Attribute("vfovOff"));
            g.camActiveOff = uint32_t(parseU64(c->Attribute("activeOff")));
            g.camFovSettingOff = parseU64(c->Attribute("fovSettingOff"));
            g.camFovRefAspect  = c->FloatAttribute("fovRefAspect", 0.0f);
        }
        games.push_back(std::move(g));
    }

    if (games.empty()) {
        games_ = defaultGameConfigs();
        configStatus_ = "config.xml has no <game> entries — using defaults";
        return false;
    }

    games_ = std::move(games);
    configStatus_ = "loaded " + std::to_string(games_.size()) + " games from config.xml";
    return true;
}

MccReader::~MccReader() { detach(); }

void MccReader::detach() {
    if (proc_ && ownsHandle_) CloseHandle((HANDLE)proc_);
    proc_ = nullptr;
    ownsHandle_ = false;
    pid_ = 0;
    dllBase_ = 0;
    rwBase_ = 0;
    rwSize_ = 0;
    cfg_ = nullptr;
    scanObj_ = scanPlr_ = 0;
    scanTried_ = false;
    tlsBlock_ = 0;
    tagGlobal_ = 0;
    tagHdr_ = tagEntries_ = 0;
    tagStride_ = 0;
    tagCap_ = 0;
    headerMaxEntries_ = 0;
}

bool MccReader::readBytes(uint64_t addr, void* buf, size_t n) const {
    if (!proc_) return false;
    SIZE_T got = 0;
    if (!ReadProcessMemory((HANDLE)proc_, (LPCVOID)addr, buf, n, &got)) return false;
    return got == n;
}

static uint32_t findGamePid(const wchar_t* exe) {
    DWORD pids[2048];
    DWORD needed = 0;
    if (!EnumProcesses(pids, sizeof(pids), &needed)) return 0;
    const DWORD count = needed / sizeof(DWORD);

    uint32_t best = 0;
    SIZE_T   bestMem = 0;
    for (DWORD i = 0; i < count; ++i) {
        if (pids[i] == 0) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pids[i]);
        if (!h) continue;

        wchar_t name[MAX_PATH] = {};
        if (GetModuleBaseNameW(h, nullptr, name, MAX_PATH) &&
            _wcsicmp(name, exe) == 0) {
            PROCESS_MEMORY_COUNTERS pmc = {};
            SIZE_T mem = 0;
            if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) mem = pmc.WorkingSetSize;
            if (mem >= bestMem) { bestMem = mem; best = pids[i]; }
        }
        CloseHandle(h);
    }
    return best;
}

static bool findRwBase(HANDLE proc, uint64_t dllBase, uint64_t& rwBase, uint64_t& rwSize) {
    uint8_t dos[64] = {};
    SIZE_T got = 0;
    if (!ReadProcessMemory(proc, (LPCVOID)dllBase, dos, sizeof(dos), &got) || got != sizeof(dos))
        return false;
    if (dos[0] != 'M' || dos[1] != 'Z') return false;

    uint32_t peOff = *reinterpret_cast<uint32_t*>(&dos[0x3C]);

    uint8_t coff[24] = {};
    if (!ReadProcessMemory(proc, (LPCVOID)(dllBase + peOff), coff, sizeof(coff), &got) || got != sizeof(coff))
        return false;
    uint16_t numSections = *reinterpret_cast<uint16_t*>(&coff[6]);
    uint16_t optHdrSize  = *reinterpret_cast<uint16_t*>(&coff[20]);
    if (numSections == 0 || numSections > 96) return false;

    uint64_t secTable = dllBase + peOff + 24 + optHdrSize;
    std::vector<uint8_t> sec(size_t(numSections) * 40);
    if (!ReadProcessMemory(proc, (LPCVOID)secTable, sec.data(), sec.size(), &got) || got != sec.size())
        return false;

    uint64_t base = 0, end = 0;
    for (uint16_t i = 0; i < numSections; ++i) {
        const uint8_t* s = &sec[size_t(i) * 40];
        uint32_t chars = *reinterpret_cast<const uint32_t*>(&s[36]);
        uint32_t vsize = *reinterpret_cast<const uint32_t*>(&s[8]);
        uint32_t vaddr = *reinterpret_cast<const uint32_t*>(&s[12]);
        if (chars & 0x80000000u) {
            if (base == 0) base = dllBase + vaddr;
            end = dllBase + vaddr + vsize;
        }
    }
    if (base == 0) return false;
    rwBase = base;
    rwSize = end - base;
    return true;
}

bool MccReader::tryAttach() {
    detach();

    if (games_.empty()) games_ = defaultGameConfigs();

    struct HostProc { std::wstring exe; uint32_t pid; HANDLE h; };
    std::vector<HostProc> hosts;
    hosts.reserve(games_.size());
    auto host = [&](const std::wstring& exe) -> HostProc* {
        for (HostProc& hp : hosts)
            if (_wcsicmp(hp.exe.c_str(), exe.c_str()) == 0) return hp.pid ? &hp : nullptr;
        HostProc hp{ exe, findGamePid(exe.c_str()), nullptr };
        if (hp.pid)
            hp.h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, hp.pid);
        if (!hp.h) hp.pid = 0;
        hosts.push_back(hp);
        return hosts.back().pid ? &hosts.back() : nullptr;
    };

    const GameConfig* hit = nullptr;
    HostProc* hitHost = nullptr;
    uint64_t hitDll = 0, hitRwBase = 0, hitRwSize = 0;

    for (const GameConfig& g : games_) {
        HostProc* hp = host(g.process.empty() ? std::wstring(kMccProcess) : g.process);
        if (!hp) continue;

        HMODULE mods[1024];
        DWORD needed = 0;
        if (!EnumProcessModulesEx(hp->h, mods, sizeof(mods), &needed, LIST_MODULES_ALL))
            continue;
        const DWORD modCount = needed / sizeof(HMODULE);

        for (DWORD i = 0; i < modCount && !hit; ++i) {
            wchar_t path[MAX_PATH] = {};
            if (!GetModuleFileNameExW(hp->h, mods[i], path, MAX_PATH)) continue;

            std::wstring lower(path);
            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
            if (lower.find(g.dll) == std::wstring::npos) continue;

            uint64_t dllBase = (uint64_t)mods[i];
            uint64_t rwBase = 0, rwSize = 0;
            if (!findRwBase(hp->h, dllBase, rwBase, rwSize)) continue;

            hit = &g; hitHost = hp;
            hitDll = dllBase; hitRwBase = rwBase; hitRwSize = rwSize;
        }
        if (hit) break;
    }

    for (HostProc& hp : hosts)
        if (hp.h && &hp != hitHost) CloseHandle(hp.h);

    if (!hit) return false;

    proc_       = hitHost->h;
    ownsHandle_ = true;
    pid_        = hitHost->pid;
    dllBase_    = hitDll;
    rwBase_     = hitRwBase;
    rwSize_     = hitRwSize;
    cfg_        = hit;
    return true;
}

bool MccReader::attachSelf() {
    detach();
    if (games_.empty()) games_ = defaultGameConfigs();

    HANDLE h = GetCurrentProcess();
    HMODULE mods[1024];
    DWORD needed = 0;
    if (!EnumProcessModulesEx(h, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) return false;
    const DWORD modCount = needed / sizeof(HMODULE);

    for (const GameConfig& g : games_) {
        for (DWORD i = 0; i < modCount; ++i) {
            wchar_t path[MAX_PATH] = {};
            if (!GetModuleFileNameExW(h, mods[i], path, MAX_PATH)) continue;
            std::wstring lower(path);
            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
            if (lower.find(g.dll) == std::wstring::npos) continue;

            uint64_t dllBase = (uint64_t)mods[i];
            uint64_t rwBase = 0, rwSize = 0;
            if (!findRwBase(h, dllBase, rwBase, rwSize)) continue;

            proc_       = h;
            ownsHandle_ = false;
            pid_        = GetCurrentProcessId();
            dllBase_    = dllBase;
            rwBase_     = rwBase;
            rwSize_     = rwSize;
            cfg_        = &g;
            return true;
        }
    }
    return false;
}

uint64_t MccReader::scanRwPool(const char* name) const {
    if (cfg_->mode != PoolAddrMode::RwLivePtr || rwSize_ == 0) return 0;
    const GameConfig& g = *cfg_;
    const size_t nlen = std::strlen(name);
    const uint64_t maxScan = (rwSize_ < (64ull << 20)) ? rwSize_ : (64ull << 20);

    const size_t chunk = 0x10000, overlap = 0x80;
    std::vector<uint8_t> buf(chunk + overlap);
    for (uint64_t off = 0; off < maxScan; off += chunk) {
        size_t want = size_t(std::min<uint64_t>(chunk + overlap, maxScan - off));
        SIZE_T got = 0;
        ReadProcessMemory((HANDLE)proc_, (LPCVOID)(rwBase_ + off), buf.data(), want, &got);
        if (got < nlen + 1) continue;

        for (size_t i = 0; i + nlen + 1 <= got; ++i) {
            if (std::memcmp(&buf[i], name, nlen) != 0 || buf[i + nlen] != 0) continue;

            const uint64_t candidate = off + i;
            uint64_t livePtr = 0;
            if (!read(rwBase_ + candidate + g.livePtrOff, livePtr) || livePtr < 0x10000) continue;
            std::vector<uint8_t> hdr(g.hdrSize);
            if (!readBytes(livePtr - g.hdrSize, hdr.data(), hdr.size())) continue;
            uint32_t sig = 0; std::memcpy(&sig, &hdr[g.offSig], 4);
            if (sig != kDataArraySignature) continue;
            if (std::memcmp(hdr.data(), name, nlen) != 0 || hdr[nlen] != 0) continue;
            return candidate;
        }
    }
    return 0;
}

uint64_t MccReader::resolveTlsBlock() const {
    if (tlsBlock_) return tlsBlock_;
    if (!proc_ || !cfg_) return 0;

    uint8_t dos[64] = {};
    if (!readBytes(dllBase_, dos, sizeof(dos))) return 0;
    if (dos[0] != 'M' || dos[1] != 'Z') return 0;
    const uint32_t peOff = *reinterpret_cast<uint32_t*>(&dos[0x3C]);

    uint16_t magic = 0;
    if (!read(dllBase_ + peOff + 24, magic) || magic != 0x20B) return 0;

    uint64_t preferredBase = 0;
    if (!read(dllBase_ + peOff + 24 + 24, preferredBase)) return 0;

    uint32_t tlsRva = 0, tlsSize = 0;
    if (!read(dllBase_ + peOff + 24 + 112 + 9 * 8, tlsRva)) return 0;
    if (!read(dllBase_ + peOff + 24 + 112 + 9 * 8 + 4, tlsSize)) return 0;
    if (!tlsRva || tlsSize < 0x28) return 0;

    uint64_t addressOfIndex = 0;
    if (!read(dllBase_ + tlsRva + 16, addressOfIndex) || !addressOfIndex) return 0;
    const uint64_t indexAddr = addressOfIndex - preferredBase + dllBase_;

    uint32_t tlsIndex = 0;
    if (!read(indexAddr, tlsIndex) || tlsIndex > 4096) return 0;

    using PFN_NtQueryInformationThread =
        LONG(__stdcall*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static auto ntQuery = reinterpret_cast<PFN_NtQueryInformationThread>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
    if (!ntQuery) return 0;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    uint64_t found = 0;
    THREADENTRY32 te = { sizeof(te) };
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid_) continue;
            HANDLE th = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (!th) continue;

            struct { PVOID ExitStatus_; PVOID TebBaseAddress; PVOID uid[2];
                     ULONG_PTR Affinity; LONG Prio; LONG BasePrio; } tbi = {};
            if (ntQuery(th, 0 , &tbi, sizeof(tbi), nullptr) == 0 &&
                tbi.TebBaseAddress) {
                uint64_t tlsPtr = 0;
                if (read((uint64_t)tbi.TebBaseAddress + 0x58, tlsPtr) && tlsPtr > 0x10000) {
                    uint64_t block = 0;
                    if (read(tlsPtr + uint64_t(tlsIndex) * 8, block) && block > 0x10000) {
                        uint64_t hdr = 0;
                        if (read(block + cfg_->objectLoc, hdr) && hdr > 0x10000) {
                            uint32_t sig = 0;
                            if (read(hdr + cfg_->offSig, sig) && sig == kDataArraySignature)
                                found = block;
                        }
                    }
                }
            }
            CloseHandle(th);
        } while (!found && Thread32Next(snap, &te));
    }
    CloseHandle(snap);

    tlsBlock_ = found;
    return found;
}

MccReader::PoolInfo MccReader::resolvePool(uint64_t loc, bool requireSig) const {
    PoolInfo info;
    const GameConfig& g = *cfg_;

    uint64_t headerAddr = 0;
    switch (g.mode) {
        case PoolAddrMode::RwLivePtr: {
            uint64_t livePtr = 0;
            if (!read(rwBase_ + loc + g.livePtrOff, livePtr)) return info;
            if (livePtr < 0x10000) return info;
            headerAddr = livePtr - g.hdrSize;
            break;
        }
        case PoolAddrMode::DllPtr: {
            uint64_t ptr = 0;
            if (!read(dllBase_ + loc, ptr)) return info;
            if (ptr < 0x10000) return info;
            headerAddr = ptr;
            break;
        }
        case PoolAddrMode::DllPtrMinus4: {
            uint64_t ptr = 0;
            if (!read(dllBase_ + loc, ptr)) return info;
            if (ptr < 0x10000) return info;
            headerAddr = ptr - 4;
            break;
        }
        case PoolAddrMode::TlsPtr: {
            const uint64_t block = resolveTlsBlock();
            if (!block) return info;
            uint64_t ptr = 0;
            if (!read(block + loc, ptr)) return info;
            if (ptr < 0x10000) return info;
            headerAddr = ptr;
            break;
        }
    }

    std::vector<uint8_t> hdr(g.hdrSize);
    if (!readBytes(headerAddr, hdr.data(), hdr.size())) return info;

    uint32_t sig = *reinterpret_cast<uint32_t*>(&hdr[g.offSig]);
    if (requireSig && sig != kDataArraySignature) return info;

    if (g.fields16) {
        info.maxEntries = *reinterpret_cast<int16_t*>(&hdr[g.offMax]);
        info.dataSizeof = *reinterpret_cast<int16_t*>(&hdr[g.offSizeof]);
        info.capacity   = *reinterpret_cast<int16_t*>(&hdr[g.offCap]);
        info.nextIndex  = *reinterpret_cast<int16_t*>(&hdr[g.offNext]);
    } else {
        info.maxEntries = *reinterpret_cast<int32_t*>(&hdr[g.offMax]);
        info.dataSizeof = *reinterpret_cast<int32_t*>(&hdr[g.offSizeof]);
        info.capacity   = *reinterpret_cast<int32_t*>(&hdr[g.offCap]);
        info.nextIndex  = *reinterpret_cast<int32_t*>(&hdr[g.offNext]);
    }

    if (info.maxEntries <= 0 || info.maxEntries > 100000 ||
        info.dataSizeof <= 0 || info.dataSizeof > 0x100000)
        return info;

    info.headerAddr = headerAddr;
    info.dataStart  = headerAddr + g.hdrSize;
    info.ok = true;
    return info;
}

bool MccReader::sample(PoolSnapshot& out) {
    out = PoolSnapshot{};
    if (!attached()) return false;
    const GameConfig& g = *cfg_;

    if (g.mode == PoolAddrMode::RwLivePtr && !scanTried_) {
        if (!resolvePool(g.objectLoc, true).ok) scanObj_ = scanRwPool("object");
        if (!resolvePool(g.playerLoc, false).ok) scanPlr_ = scanRwPool("players");
        scanTried_ = true;
    }
    const uint64_t objLoc = scanObj_ ? scanObj_ : g.objectLoc;
    const uint64_t plrLoc = scanPlr_ ? scanPlr_ : g.playerLoc;

    PoolInfo obj = resolvePool(objLoc, true);
    if (!obj.ok && g.mode == PoolAddrMode::TlsPtr && tlsBlock_) {
        tlsBlock_ = 0;
        obj = resolvePool(objLoc, true);
    }
    if (!obj.ok) return false;

    headerMaxEntries_ = obj.maxEntries;
    int n = (maxEntriesOverride_ > 0) ? maxEntriesOverride_ : obj.maxEntries;
    if (n > 200000) n = 200000;
    const bool extended = n > obj.maxEntries;
    const int cap = extended ? n : std::min(obj.capacity, n);

    std::vector<uint8_t> objData(size_t(n) * obj.dataSizeof);
    if (extended) {
        std::memset(objData.data(), 0, objData.size());
        SIZE_T got = 0;
        ReadProcessMemory((HANDLE)proc_, (LPCVOID)obj.dataStart, objData.data(), objData.size(), &got);
        if (got == 0) return false;
    } else {
        if (!readBytes(obj.dataStart, objData.data(), objData.size())) return false;
    }

    out.occ.assign(n, 0);
    out.isPlayer.assign(n, 0);
    out.cat.assign(n, uint8_t(gTypes.unknownId()));
    out.typeByte.assign(n, 0);
    out.objAddr.assign(n, 0);
    out.salt.assign(n, 0);
    out.playerIndex.assign(n, -1);
    out.maxEntries = n;
    out.capacity   = cap;
    out.nextIndex  = -1;
    out.objDataStart  = obj.dataStart;
    out.objDataSizeof = obj.dataSizeof;

    const uint64_t objMemBase = obj.dataStart + uint64_t(n) * obj.dataSizeof;
    const uint64_t loopBase   = obj.headerAddr + g.loopBaseOff;

    auto readCat = [&](int i) -> uint8_t {
        uint8_t raw = objData[size_t(i) * obj.dataSizeof + g.objTypeOff];
        out.typeByte[i] = raw;
        return uint8_t(gTypes.categoryFor(g.enumStyle, raw));
    };
    auto objAddrForSlot = [&](int i) -> uint64_t {
        const uint8_t* e = &objData[size_t(i) * obj.dataSizeof + g.objPtrOff];
        switch (g.baseMode) {
            case ObjBaseMode::AbsPtr:     return *reinterpret_cast<const uint64_t*>(e);
            case ObjBaseMode::OffsetMem:  return objMemBase + *reinterpret_cast<const uint32_t*>(e);
            case ObjBaseMode::OffsetLoop: return loopBase   + *reinterpret_cast<const uint32_t*>(e);
        }
        return 0;
    };

    for (int i = 0; i < cap; ++i) {
        uint16_t salt = *reinterpret_cast<uint16_t*>(&objData[size_t(i) * obj.dataSizeof]);
        if (salt != 0) {
            out.occ[i]     = 1;
            out.cat[i]     = readCat(i);
            out.objAddr[i] = objAddrForSlot(i);
            out.salt[i]    = int16_t(salt);
            out.objectCount++;
        }
    }

    PoolInfo plr = resolvePool(plrLoc, false);
    if (plr.ok) {
        const int pn   = plr.maxEntries;
        const int pcap = std::min(plr.capacity, pn);
        out.plrDataStart  = plr.dataStart;
        out.plrDataSizeof = plr.dataSizeof;
        std::vector<uint8_t> plrData(size_t(pn) * plr.dataSizeof);
        out.playerNames.assign(pn, std::string());
        out.playerServices.assign(pn, std::string());

        auto decodeUtf16 = [](const uint8_t* base, uint32_t off, uint32_t len) {
            std::string s;
            const uint8_t* p = base + off;
            for (uint32_t c = 0; c < len; ++c) {
                uint16_t ch = *reinterpret_cast<const uint16_t*>(p + c * 2);
                if (ch == 0) break;
                s += (ch < 0x80) ? char(ch) : '?';
            }
            return s;
        };

        if (readBytes(plr.dataStart, plrData.data(), plrData.size())) {
            for (int i = 0; i < pcap; ++i) {
                const uint8_t* e = &plrData[size_t(i) * plr.dataSizeof];
                uint16_t psalt = *reinterpret_cast<const uint16_t*>(e);
                if (psalt == 0) continue;

                if (g.playerNameLen > 0)    out.playerNames[i]    = decodeUtf16(e, g.playerNameOff, g.playerNameLen);
                if (g.playerServiceLen > 0) out.playerServices[i] = decodeUtf16(e, g.playerServiceOff, g.playerServiceLen);

                uint32_t datum = *reinterpret_cast<const uint32_t*>(e + g.playerCuOff);
                if (datum == 0 || datum == 0xFFFFFFFFu) continue;
                int idx = int(datum & 0xFFFF);
                if (idx >= 0 && idx < n) {
                    if (!out.isPlayer[idx]) out.playerCount++;
                    out.isPlayer[idx]    = 1;
                    out.playerIndex[idx] = int16_t(i);
                    if (!out.occ[idx]) {
                        out.cat[idx]     = readCat(idx);
                        out.objAddr[idx] = objAddrForSlot(idx);
                        out.salt[idx]    = *reinterpret_cast<uint16_t*>(
                                               &objData[size_t(idx) * obj.dataSizeof]);
                    }
                    out.occ[idx] = 1;
                }
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        if (!out.occ[i]) { out.nextIndex = i; break; }
    }

    out.valid = true;
    return true;
}

uint64_t MccReader::scanTagGlobal() const {
    const GameConfig& g = *cfg_;
    if (rwSize_ == 0 || g.tagPoolName.empty()) return 0;

    const size_t nlen = g.tagPoolName.size();
    const uint64_t maxScan = (rwSize_ < (64ull << 20)) ? rwSize_ : (64ull << 20);
    const size_t chunk = 0x10000;
    std::vector<uint8_t> buf(chunk + 8);

    for (uint64_t off = 0; off + 8 <= maxScan; off += chunk) {
        size_t want = size_t(std::min<uint64_t>(chunk + 8, maxScan - off));
        SIZE_T got = 0;
        ReadProcessMemory((HANDLE)proc_, (LPCVOID)(rwBase_ + off), buf.data(), want, &got);
        if (got < 8) continue;

        for (size_t i = 0; i + 8 <= got; i += 8) {
            uint64_t cand = 0;
            std::memcpy(&cand, &buf[i], 8);
            if (cand < 0x10000) continue;

            std::vector<uint8_t> hdr(g.hdrSize);
            if (!readBytes(cand, hdr.data(), hdr.size())) continue;
            uint32_t sig = 0; std::memcpy(&sig, &hdr[g.offSig], 4);
            if (sig != kDataArraySignature) continue;
            if (std::memcmp(hdr.data(), g.tagPoolName.c_str(), nlen) != 0 || hdr[nlen] != 0)
                continue;
            return (rwBase_ + off + i) - dllBase_;
        }
    }
    return 0;
}

bool MccReader::resolveTagArray(uint64_t& entries, uint32_t& stride, int& cap) const {
    const GameConfig& g = *cfg_;

    auto readHdrPtr = [&](uint64_t rva, uint64_t& hdr) {
        return rva != 0 && read(dllBase_ + rva, hdr) && hdr >= 0x10000;
    };

    uint64_t hdrAddr = 0;
    if (!readHdrPtr(tagGlobal_ ? tagGlobal_ : g.tagA, hdrAddr)) {
        uint64_t found = scanTagGlobal();
        if (!found || !readHdrPtr(found, hdrAddr)) return false;
        tagGlobal_ = found;
    }

    if (hdrAddr == tagHdr_ && tagEntries_ && tagStride_) {
        entries = tagEntries_; stride = tagStride_; cap = tagCap_;
        return true;
    }

    std::vector<uint8_t> hdr(g.hdrSize);
    if (!readBytes(hdrAddr, hdr.data(), hdr.size())) return false;
    uint32_t sig = 0; std::memcpy(&sig, &hdr[g.offSig], 4);
    if (sig != kDataArraySignature) return false;
    if (!g.tagPoolName.empty()) {
        const size_t nlen = g.tagPoolName.size();
        if (nlen + 1 > g.hdrSize ||
            std::memcmp(hdr.data(), g.tagPoolName.c_str(), nlen) != 0 || hdr[nlen] != 0)
            return false;
    }

    int32_t szof = 0, capacity = 0;
    std::memcpy(&szof, &hdr[g.offSizeof], 4);
    std::memcpy(&capacity, &hdr[g.offCap], 4);
    if (szof <= 0 || szof > 0x10000 || capacity < 0) return false;

    uint64_t data = 0;
    std::memcpy(&data, &hdr[g.livePtrOff], 8);
    if (data != hdrAddr + g.hdrSize) data = hdrAddr + g.hdrSize;

    tagHdr_ = hdrAddr; tagEntries_ = data;
    tagStride_ = uint32_t(szof); tagCap_ = capacity;
    entries = data; stride = tagStride_; cap = capacity;
    return true;
}

std::string MccReader::readCString(uint64_t addr) const {
    if (addr < 0x10000) return {};
    char buf[256] = {};
    if (!readBytes(addr, buf, sizeof(buf) - 1)) return {};
    buf[sizeof(buf) - 1] = '\0';
    return std::string(buf);
}

std::string MccReader::tagName(uint32_t datum, uint64_t* outAddr) const {
    const GameConfig& g = *cfg_;
    const uint16_t index = uint16_t(datum & 0xFFFF);
    uint64_t namePtr = 0;
    switch (g.tagMode) {
        case TagNameMode::None:
            return {};
        case TagNameMode::TagInstance: {
            uint64_t entries = 0; uint32_t stride = 0; int cap = 0;
            if (!resolveTagArray(entries, stride, cap)) return {};
            if (cap <= 0 || index >= cap) return {};
            const uint64_t rec = entries + uint64_t(index) * stride;
            uint16_t salt = 0;
            if (!read(rec + g.tagRecSaltOff, salt)) return {};
            if (salt != uint16_t(datum >> 16)) return {};
            if (!read(rec + g.tagRecNameOff, namePtr)) return {};
            break;
        }
        case TagNameMode::PtrArray: {
            if (!read(rwBase_ + g.tagA + uint64_t(index) * 8, namePtr)) return {};
            break;
        }
        case TagNameMode::StructArray: {
            uint64_t structPtr = 0;
            if (!read(rwBase_ + g.tagA, structPtr) || structPtr < 0x10000) return {};
            if (!read(structPtr + 0x820000 + uint64_t(index) * 8, namePtr)) return {};
            break;
        }
        case TagNameMode::H2StringTable: {
            uint64_t meta = 0, table = 0;
            if (!read(dllBase_ + g.tagA, meta) || meta < 0x10000) return {};
            if (!read(dllBase_ + g.tagB, table) || table < 0x10000) return {};
            uint32_t strOff = 0;
            if (!read(meta + uint64_t(index) * 4, strOff)) return {};
            namePtr = table + strOff;
            break;
        }
        case TagNameMode::H1Rebase: {
            uint64_t instBase = 0;
            if (!read(dllBase_ + g.tagA, instBase) || instBase < 0x10000) return {};
            uint32_t stored = 0;
            if (!read(instBase + uint64_t(index) * 0x20 + 0x10, stored) || stored == 0) return {};
            uint64_t oldBase = 0, newBase = 0;
            if (!read(dllBase_ + g.tagB, oldBase) || !read(dllBase_ + g.tagC, newBase)) return {};
            namePtr = uint64_t(stored) - oldBase + newBase;
            break;
        }
    }
    if (outAddr) *outAddr = namePtr;
    return readCString(namePtr);
}

bool MccReader::objectDetail(uint64_t objAddr, ObjectDetail& out) const {
    out = ObjectDetail{};
    if (!attached() || objAddr < 0x10000) return false;
    const GameConfig& g = *cfg_;

    out.tagDatumAddr = uint64_t(int64_t(objAddr) + g.tagDatumOff);
    uint32_t tagDatum = 0;
    if (read(out.tagDatumAddr, tagDatum) &&
        tagDatum != 0 && tagDatum != 0xFFFFFFFFu) {
        out.tagDatum = tagDatum;
        std::string name = tagName(tagDatum, &out.tagNameAddr);
        if (!name.empty()) {
#ifdef _MSC_VER
            strncpy_s(out.tag, name.c_str(), _TRUNCATE);
#else
            std::strncpy(out.tag, name.c_str(), sizeof(out.tag) - 1);
#endif
            out.hasTag = true;
        }
    }

    out.posAddr = uint64_t(int64_t(objAddr) + g.posOff);
    float p[3] = {};
    if (readBytes(out.posAddr, p, sizeof(p))) {
        out.pos[0] = p[0]; out.pos[1] = p[1]; out.pos[2] = p[2];
        out.hasPos = true;
    }

    if (g.oriOff != 0) {
        float v[6] = {};
        if (readBytes(uint64_t(int64_t(objAddr) + g.oriOff), v, sizeof(v))) {
            const float* fwd = &v[0];
            const float* up  = &v[3];
            const float rad2deg = 57.29577951308232f;

            out.yaw   = std::atan2(fwd[1], fwd[0]) * rad2deg;
            out.pitch = std::atan2(fwd[2],
                        std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1])) * rad2deg;

            const float rx = fwd[1], ry = -fwd[0];
            const float rlen = std::sqrt(rx * rx + ry * ry);
            if (rlen > 1e-6f) {
                const float nx = rx / rlen, ny = ry / rlen;
                const float ux = -ny * fwd[2], uy = nx * fwd[2],
                            uz = nx * fwd[1] - ny * fwd[0];
                out.roll = std::atan2(up[0] * nx + up[1] * ny,
                                      up[0] * ux + up[1] * uy + up[2] * uz) * rad2deg;
            } else {
                out.roll = 0.0f;
            }
            out.hasOri = true;
        }
    }

    auto readDatum = [&](int64_t off, uint32_t& datum, bool& has, uint64_t* addr) {
        if (off == 0) return;
        uint64_t a = uint64_t(int64_t(objAddr) + off);
        uint32_t v = 0;
        if (read(a, v) && v != 0 && v != 0xFFFFFFFFu) { datum = v; has = true; }
        if (addr) *addr = a;
    };
    readDatum(g.parentOff,  out.parentDatum,  out.hasParent,   &out.parentDatumAddr);
    readDatum(g.childOff,   out.childDatum,   out.hasChildren, &out.childDatumAddr);
    readDatum(g.siblingOff, out.siblingDatum, out.hasSibling,  nullptr);

    out.ok = out.hasTag || out.hasPos;
    return out.ok;
}

bool MccReader::attachmentInfo(const PoolSnapshot& snap, const ObjectDetail& d,
                               Attachment& out) const {
    out = Attachment{};
    if (!attached()) return false;

    auto describe = [&](uint32_t datum, std::string& label, std::string& path) {
        label.clear(); path.clear();
        const int idx = int(datum & 0xFFFF);
        if (idx < 0 || idx >= snap.maxEntries) return;
        if (idx >= (int)snap.occ.size() || !snap.occ[idx]) return;
        std::string name;
        const uint64_t a = idx < (int)snap.objAddr.size() ? snap.objAddr[idx] : 0;
        if (a) {
            ObjectDetail od;
            if (objectDetail(a, od) && od.hasTag) {
                const char* s = std::strrchr(od.tag, '\\');
                name = s ? s + 1 : od.tag;
                path = od.tag;
            }
        }
        const std::string type = gTypes.cat(snap.cat[idx]).name;
        char buf[160];
        if (!name.empty())
            std::snprintf(buf, sizeof(buf), "%s (%s) #%d", name.c_str(), type.c_str(), idx);
        else
            std::snprintf(buf, sizeof(buf), "%s #%d", type.c_str(), idx);
        label = buf;
    };

    if (d.hasParent) {
        describe(d.parentDatum, out.parent, out.parentPath);
        if (!out.parent.empty()) out.parentSlot = int(d.parentDatum & 0xFFFF);
    }

    if (d.hasChildren) {
        uint32_t cur = d.childDatum;
        for (int guard = 0; guard < 16 && cur != 0 && cur != 0xFFFFFFFFu; ++guard) {
            const int idx = int(cur & 0xFFFF);
            if (idx < 0 || idx >= snap.maxEntries) break;
            if (idx >= (int)snap.occ.size() || !snap.occ[idx]) break;
            const uint64_t a = idx < (int)snap.objAddr.size() ? snap.objAddr[idx] : 0;
            if (!a) break;
            ObjectDetail cd;
            if (!objectDetail(a, cd)) break;

            const std::string type = gTypes.cat(snap.cat[idx]).name;
            std::string nm;
            if (cd.hasTag) {
                const char* s = std::strrchr(cd.tag, '\\');
                nm = std::string(s ? s + 1 : cd.tag) + " (" + type + ")";
                if (!out.childrenPaths.empty()) out.childrenPaths += ", ";
                out.childrenPaths += cd.tag;
            } else {
                nm = type;
            }
            if (!out.children.empty()) out.children += ", ";
            out.children += nm;
            out.childSlots.push_back(idx);

            if (!cd.hasSibling) break;
            cur = cd.siblingDatum;
        }
    }

    return !out.parent.empty() || !out.children.empty();
}

bool MccReader::objectPosition(uint64_t objAddr, float out[3]) const {
    if (!attached() || objAddr < 0x10000) return false;
    return readBytes(uint64_t(int64_t(objAddr) + cfg_->posOff), out, sizeof(float) * 3);
}

bool MccReader::cameraData(CameraData& out) const {
    if (!attached()) return false;
    const GameConfig& g = *cfg_;

    uint64_t base = 0;
    switch (g.cameraMode) {
        case CameraMode::None: return false;
        case CameraMode::Module:
            base = dllBase_ + g.camOff0;
            break;
        case CameraMode::ModulePtr: {
            uint64_t p = 0;
            if (!read(dllBase_ + g.camOff0, p) || p < 0x10000) return false;
            base = p + g.camOff1;
            break;
        }
        case CameraMode::TlsObserver: {
            const uint64_t block = resolveTlsBlock();
            if (!block) return false;
            uint64_t observers = 0;
            if (!read(block + g.camOff0, observers) || observers < 0x10000) return false;
            base = observers + g.camOff1;
            if (g.camActiveOff) {
                uint8_t active = 0;
                if (!read(base + g.camActiveOff, active) || !active) return false;
            }
            break;
        }
    }

    if (!readBytes(base + g.camPosOff, out.pos, sizeof(out.pos))) return false;
    if (!readBytes(base + g.camFwdOff, out.fwd, sizeof(out.fwd))) return false;
    if (!readBytes(base + g.camUpOff,  out.up,  sizeof(out.up)))  return false;
    if (!read(base + g.camFovOff, out.fovH)) return false;

    if (g.camVFovOff != 0) {
        float v = 0.0f;
        if (read(dllBase_ + g.camVFovOff, v) && v > 0.0001f && v < 3.14159f) out.fovV = v;
    }

    if (g.camFovSettingOff != 0 && g.camFovRefAspect > 0.0f) {
        float deg = 0.0f;
        if (read(dllBase_ + g.camFovSettingOff, deg) && deg > 1.0f && deg < 179.0f) {
            const float h = deg * 0.017453292519943295f;
            out.fovH = h;
            out.fovV = 2.0f * std::atan(std::tan(h * 0.5f) / g.camFovRefAspect);
        }
    }
    return true;
}
