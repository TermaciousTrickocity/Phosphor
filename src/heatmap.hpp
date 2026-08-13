#pragma once

#include <cstdint>
#include <vector>

#include "imgui.h"
#include "mcc.hpp"

struct HeatmapSettings {
    int   columns   = 48;
    float cellSize  = 14.0f;
    float gap       = 2.0f;
    float halfLife  = 0.03f;
    bool  paused    = false;
};

class Heatmap {
public:
    void update(const PoolSnapshot& snap, float dt, const HeatmapSettings& s);

    void draw(const HeatmapSettings& s, int selectedSlot,
              int parentSlot = -1, const std::vector<int>& childSlots = {});

    void drawOverlay(ImDrawList* dl, ImVec2 origin, const HeatmapSettings& s) const;

    int gridCols(const HeatmapSettings& s) const { return s.columns < 1 ? 1 : s.columns; }
    int gridRows(const HeatmapSettings& s) const {
        const int c = gridCols(s);
        return ((int)intensity_.size() + c - 1) / c;
    }

    int liveObjects() const { return liveObjects_; }
    int livePlayers() const { return livePlayers_; }

    uint32_t presentCats() const { return presentMask_; }

    int   hoveredSlot() const { return hovered_; }
    int   clickedSlot() const { return clicked_; }
    int   count()       const { return (int)intensity_.size(); }
    float intensityAt(int i) const { return (i >= 0 && i < count()) ? intensity_[i] : 0.0f; }
    uint8_t catAt(int i)     const { return (i >= 0 && i < count()) ? cat_[i] : uint8_t(gTypes.unknownId()); }
    bool  isPlayerAt(int i)  const { return (i >= 0 && i < count()) && player_[i]; }

    float filledFor(int i) const {
        return (i >= 0 && i < count() && filled_[i]) ? clock_ - since_[i] : -1.0f;
    }
    float vacantFor(int i) const {
        return (i >= 0 && i < count() && !filled_[i] && touched_[i]) ? clock_ - since_[i] : -1.0f;
    }
    bool  ageExact(int i) const { return i >= 0 && i < count() && exact_[i]; }

private:
    std::vector<float>   intensity_;
    std::vector<uint8_t> cat_;
    std::vector<uint8_t> player_;
    std::vector<uint8_t> touched_;
    std::vector<float>   since_;
    std::vector<uint8_t> filled_;
    std::vector<int16_t> heldSalt_;
    std::vector<uint8_t> exact_;
    int      liveObjects_ = 0;
    int      livePlayers_ = 0;
    uint32_t presentMask_ = 0;
    int      hovered_     = -1;
    int      clicked_     = -1;
    int      nextIndex_   = -1;
    float    time_        = 0.0f;
    float    clock_       = 0.0f;
    bool     firstPass_   = true;
};
