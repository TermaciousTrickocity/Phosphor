#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "objtypes.hpp"

enum class PoolAddrMode {
    RwLivePtr,
    DllPtr,
    DllPtrMinus4,
    TlsPtr,
};

enum class ObjBaseMode {
    AbsPtr,
    OffsetMem,
    OffsetLoop,
};

enum class CameraMode {
    None,
    Module,
    ModulePtr,
    TlsObserver,
};

struct CameraData {
    float pos[3] = {};
    float fwd[3] = {};
    float up[3]  = {};
    float fovH   = 0.0f;
    float fovV   = 0.0f;
};

enum class TagNameMode {
    None,
    TagInstance,
    PtrArray,
    StructArray,
    H2StringTable,
    H1Rebase,
};

struct ObjectDetail {
    bool  ok       = false;
    char  tag[256] = {};
    bool  hasTag   = false;
    uint32_t tagDatum = 0;
    float pos[3]   = {};
    bool  hasPos   = false;
    float yaw = 0, pitch = 0, roll = 0;
    bool  hasOri   = false;

    uint32_t parentDatum  = 0; bool hasParent   = false;
    uint32_t childDatum   = 0; bool hasChildren = false;
    uint32_t siblingDatum = 0; bool hasSibling  = false;

    uint64_t tagDatumAddr    = 0;
    uint64_t tagNameAddr     = 0;
    uint64_t posAddr         = 0;
    uint64_t parentDatumAddr = 0;
    uint64_t childDatumAddr  = 0;
};

struct Attachment {
    std::string parent;
    std::string parentPath;
    std::string children;
    std::string childrenPaths;
    int parentSlot = -1;
    std::vector<int> childSlots;
};

struct GameConfig {
    std::wstring process;
    std::wstring dll;
    std::string  name;

    PoolAddrMode mode;
    uint64_t objectLoc;
    uint64_t playerLoc;
    uint64_t livePtrOff;

    uint32_t hdrSize;
    uint32_t offMax;
    uint32_t offSizeof;
    uint32_t offCap;
    uint32_t offNext;
    uint32_t offSig;
    bool     fields16;

    uint32_t     playerCuOff;
    uint32_t     playerNameOff;
    uint32_t     playerNameLen;
    uint32_t     playerServiceOff;
    uint32_t     playerServiceLen;
    uint32_t     objTypeOff;
    std::string  enumStyle;

    ObjBaseMode baseMode;
    uint32_t    objPtrOff;
    uint64_t    loopBaseOff;
    int64_t     tagDatumOff;
    int64_t     posOff;
    int64_t     oriOff;
    int64_t     parentOff;
    int64_t     childOff;
    int64_t     siblingOff;

    TagNameMode tagMode;
    uint64_t    tagA;
    uint64_t    tagB;
    uint64_t    tagC;

    std::string tagPoolName;
    uint32_t    tagRecNameOff;
    uint32_t    tagRecSaltOff;

    CameraMode  cameraMode;
    uint64_t    camOff0;
    uint64_t    camOff1;
    uint32_t    camPosOff;
    uint32_t    camFwdOff;
    uint32_t    camUpOff;
    uint32_t    camFovOff;
    uint64_t    camVFovOff;
    uint32_t    camActiveOff;

    uint64_t    camFovSettingOff;
    float       camFovRefAspect;
};

struct PoolSnapshot {
    bool valid = false;
    int  maxEntries = 0;
    int  capacity   = 0;
    int  nextIndex  = -1;
    int  objectCount = 0;
    int  playerCount = 0;
    uint64_t objDataStart = 0;  int objDataSizeof = 0;
    uint64_t plrDataStart = 0;  int plrDataSizeof = 0;
    std::vector<uint8_t>  occ;
    std::vector<uint8_t>  isPlayer;
    std::vector<uint8_t>  cat;
    std::vector<uint8_t>  typeByte;
    std::vector<uint64_t> objAddr;
    std::vector<int16_t>  salt;
    std::vector<int16_t>  playerIndex;
    std::vector<std::string> playerNames;
    std::vector<std::string> playerServices;
};

class MccReader {
public:
    ~MccReader();

    bool loadConfig(const std::string& path);
    const std::string& configStatus() const { return configStatus_; }
    size_t gameCount() const { return games_.size(); }

    bool attached() const { return proc_ != nullptr && cfg_ != nullptr; }
    const GameConfig* game() const { return cfg_; }
    uint32_t pid() const { return pid_; }

    void setMaxEntriesOverride(int v) { maxEntriesOverride_ = (v < 0) ? 0 : v; }
    int  maxEntriesOverride() const { return maxEntriesOverride_; }
    int  headerMaxEntries()   const { return headerMaxEntries_; }

    bool tryAttach();
    bool attachSelf();
    void detach();

    bool sample(PoolSnapshot& out);

    bool objectDetail(uint64_t objAddr, ObjectDetail& out) const;

    bool objectPosition(uint64_t objAddr, float out[3]) const;

    bool attachmentInfo(const PoolSnapshot& snap, const ObjectDetail& d, Attachment& out) const;

    bool cameraData(CameraData& out) const;

private:
    struct PoolInfo {
        bool ok = false;
        uint64_t headerAddr = 0;
        uint64_t dataStart = 0;
        int maxEntries = 0;
        int capacity = 0;
        int nextIndex = -1;
        int dataSizeof = 0;
    };

    bool readBytes(uint64_t addr, void* buf, size_t n) const;
    template <class T> bool read(uint64_t addr, T& v) const {
        return readBytes(addr, &v, sizeof(T));
    }
    PoolInfo    resolvePool(uint64_t loc, bool requireSig) const;

    bool resolveTagArray(uint64_t& entries, uint32_t& stride, int& cap) const;
    uint64_t scanTagGlobal() const;

    uint64_t resolveTlsBlock() const;
    std::string tagName(uint32_t datum, uint64_t* outAddr = nullptr) const;
    std::string readCString(uint64_t addr) const;

    uint64_t scanRwPool(const char* name) const;

    void*    proc_ = nullptr;
    bool     ownsHandle_ = false;
    uint32_t pid_  = 0;
    uint64_t dllBase_ = 0;
    uint64_t rwBase_  = 0;
    uint64_t rwSize_  = 0;
    const GameConfig* cfg_ = nullptr;

    uint64_t scanObj_ = 0, scanPlr_ = 0;
    bool     scanTried_ = false;

    mutable uint64_t tlsBlock_ = 0;

    mutable uint64_t tagGlobal_ = 0;
    mutable uint64_t tagHdr_ = 0, tagEntries_ = 0;
    mutable uint32_t tagStride_ = 0;
    mutable int      tagCap_ = 0;

    int maxEntriesOverride_ = 0;
    int headerMaxEntries_   = 0;

    std::vector<GameConfig> games_;
    std::string             configStatus_;
};
