#pragma once

#include <cstdint>

namespace ovl {

inline constexpr wchar_t  kSettingsMapping[] = L"Local\\PhosphorOverlaySettings";
inline constexpr wchar_t  kUnloadEvent[]     = L"Local\\PhosphorUnload";
inline constexpr uint32_t kMagic = 0x4F564C34u;

enum Element : uint32_t {
    Marker      = 1u << 0,
    TypeName    = 1u << 1,
    Gamertag    = 1u << 2,
    ServiceTag  = 1u << 3,
    TagName     = 1u << 4,
    TagPath     = 1u << 5,
    Coords      = 1u << 6,
    Datum       = 1u << 7,
    Salt        = 1u << 8,
    HeatmapCorner = 1u << 9,
    Attachment  = 1u << 10,
};

inline constexpr uint32_t kDefault =
    Marker | TypeName | Gamertag | TagName | Coords | HeatmapCorner;

struct Settings {
    uint32_t magic;
    uint32_t elements;
    uint32_t types;
    float    minRange;
    float    maxRange;
    float    heatCell;
    int32_t  heatCols;
};

inline void initDefaults(Settings& s) {
    s.magic    = kMagic;
    s.elements = kDefault;
    s.types    = 0xFFFFFFFFu;
    s.minRange = 0.0f;
    s.maxRange = 100000.0f;
    s.heatCell = 3.0f;
    s.heatCols = 48;
}

}
