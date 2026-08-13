#include "heatmap.hpp"

#include <cmath>
#include <cstdio>

#include "imgui.h"

static inline ImU32 lerpColor(ImU32 a, ImU32 b, float t) {
    auto ca = ImGui::ColorConvertU32ToFloat4(a);
    auto cb = ImGui::ColorConvertU32ToFloat4(b);
    ImVec4 r(ca.x + (cb.x - ca.x) * t,
             ca.y + (cb.y - ca.y) * t,
             ca.z + (cb.z - ca.z) * t,
             ca.w + (cb.w - ca.w) * t);
    return ImGui::ColorConvertFloat4ToU32(r);
}

void Heatmap::update(const PoolSnapshot& snap, float dt, const HeatmapSettings& s) {
    time_ += dt;
    if (!snap.valid) return;

    nextIndex_ = snap.nextIndex;
    const int n = snap.maxEntries;
    if ((int)intensity_.size() != n) {
        intensity_.assign(n, 0.0f);
        cat_.assign(n, uint8_t(gTypes.unknownId()));
        player_.assign(n, 0);
        touched_.assign(n, 0);
        since_.assign(n, 0.0f);
        filled_.assign(n, 0);
        heldSalt_.assign(n, 0);
        exact_.assign(n, 0);
        firstPass_ = true;
    }

    if (!s.paused) clock_ += dt;

    float decay = (s.halfLife > 0.001f) ? std::pow(0.5f, dt / s.halfLife) : 0.0f;
    if (s.paused) decay = 1.0f;

    liveObjects_ = 0;
    livePlayers_ = 0;
    presentMask_ = 0;

    for (int i = 0; i < n; ++i) {
        if (!s.paused && snap.occ[i]) {
            if (!filled_[i] || snap.salt[i] != heldSalt_[i]) {
                exact_[i]    = (firstPass_ && !filled_[i]) ? 0 : 1;
                since_[i]    = clock_;
                heldSalt_[i] = snap.salt[i];
                filled_[i]   = 1;
            }
            intensity_[i] = 1.0f;
            cat_[i]    = snap.cat[i];
            player_[i] = snap.isPlayer[i];
            touched_[i] = 1;
            if (snap.isPlayer[i]) livePlayers_++; else liveObjects_++;
        } else if (!s.paused) {
            if (filled_[i]) {
                since_[i]  = clock_;
                filled_[i] = 0;
                exact_[i]  = 1;
            }
            intensity_[i] *= decay;
            if (intensity_[i] < 0.01f) {
                intensity_[i] = 0.0f;
                cat_[i] = uint8_t(gTypes.unknownId());
                player_[i] = 0;
            }
        }
        if (intensity_[i] > 0.0f && cat_[i] < 32)
            presentMask_ |= (1u << cat_[i]);
    }

    if (!s.paused) firstPass_ = false;
}

void Heatmap::draw(const HeatmapSettings& s, int selectedSlot,
                   int parentSlot, const std::vector<int>& childSlots) {
    hovered_ = -1;
    clicked_ = -1;
    const int n = (int)intensity_.size();
    if (n == 0) {
        ImGui::TextDisabled("No pool data yet.");
        return;
    }

    const ImU32 cEmpty  = IM_COL32(28, 30, 38, 255);
    const ImU32 cFloor  = IM_COL32(48, 51, 63, 255);
    const ImU32 cBorder = IM_COL32(255, 255, 255, 235);
    const ImU32 cStar   = IM_COL32(15, 15, 20, 255);

    float pr, pg, pb;
    ImGui::ColorConvertHSVtoRGB(std::fmod(time_ * 0.22f, 1.0f), 0.85f, 1.0f, pr, pg, pb);
    const ImU32 cPlayer = IM_COL32(int(pr * 255), int(pg * 255), int(pb * 255), 255);

    const int   cols = s.columns < 1 ? 1 : s.columns;
    const int   rows = (n + cols - 1) / cols;
    const float step = s.cellSize + s.gap;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const char*  star   = "*";
    const ImVec2 starSz = ImGui::CalcTextSize(star);

    for (int i = 0; i < n; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const ImVec2 p0(origin.x + col * step, origin.y + row * step);
        const ImVec2 p1(p0.x + s.cellSize, p0.y + s.cellSize);

        const float t = intensity_[i];
        const ImU32 base = touched_[i] ? cFloor : cEmpty;
        ImU32 fill = base;
        if (player_[i] && t > 0.0f) {
            fill = lerpColor(base, cPlayer, t);
        } else if (t > 0.0f) {
            const CatInfo& c = gTypes.cat(cat_[i]);
            fill = lerpColor(base, IM_COL32(c.r, c.g, c.b, 255), t);
        }

        dl->AddRectFilled(p0, p1, fill, 2.0f);

        if (player_[i] && t > 0.0f) {
            dl->AddRect(p0, p1, cBorder, 2.0f, 0, 1.6f);
            const ImVec2 tp(p0.x + (s.cellSize - starSz.x) * 0.5f,
                            p0.y + (s.cellSize - starSz.y) * 0.5f);
            dl->AddText(tp, cStar, star);
        }
    }

    if (nextIndex_ >= 0 && nextIndex_ < n) {
        const int col = nextIndex_ % cols;
        const int row = nextIndex_ / cols;
        const ImVec2 p0(origin.x + col * step, origin.y + row * step);
        const ImVec2 p1(p0.x + s.cellSize, p0.y + s.cellSize);
        dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 255), 2.0f, 0, 2.0f);
    }

    auto outlineSlot = [&](int slot, ImU32 color) {
        if (slot < 0 || slot >= n) return;
        const int col = slot % cols;
        const int row = slot / cols;
        const ImVec2 p0(origin.x + col * step - 1, origin.y + row * step - 1);
        const ImVec2 p1(p0.x + s.cellSize + 2, p0.y + s.cellSize + 2);
        dl->AddRect(p0, p1, color, 2.0f, 0, 2.5f);
    };

    outlineSlot(parentSlot, IM_COL32(90, 230, 120, 255));
    for (int cs : childSlots) outlineSlot(cs, IM_COL32(255, 165, 40, 255));

    outlineSlot(selectedSlot, IM_COL32(90, 220, 255, 255));

    const ImVec2 total(cols * step, rows * step);
    ImGui::Dummy(total);

    if (ImGui::IsWindowHovered() &&
        ImGui::IsMouseHoveringRect(origin, ImVec2(origin.x + total.x, origin.y + total.y))) {
        const ImVec2 m = ImGui::GetMousePos();
        const int col = int((m.x - origin.x) / step);
        const int row = int((m.y - origin.y) / step);
        const int idx = row * cols + col;
        if (col >= 0 && col < cols && idx >= 0 && idx < n) {
            hovered_ = idx;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) clicked_ = idx;
        }
    }
}

void Heatmap::drawOverlay(ImDrawList* dl, ImVec2 origin, const HeatmapSettings& s) const {
    const int n = (int)intensity_.size();
    if (n == 0) return;

    const ImU32 cEmpty  = IM_COL32(24, 26, 34, 235);
    const ImU32 cFloor  = IM_COL32(44, 47, 58, 235);
    const ImU32 cBorder = IM_COL32(255, 255, 255, 235);

    float pr, pg, pb;
    ImGui::ColorConvertHSVtoRGB(std::fmod(time_ * 0.22f, 1.0f), 0.85f, 1.0f, pr, pg, pb);
    const ImU32 cPlayer = IM_COL32(int(pr * 255), int(pg * 255), int(pb * 255), 255);

    const int   cols = s.columns < 1 ? 1 : s.columns;
    const float step = s.cellSize + s.gap;

    for (int i = 0; i < n; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const ImVec2 p0(origin.x + col * step, origin.y + row * step);
        const ImVec2 p1(p0.x + s.cellSize, p0.y + s.cellSize);

        const float t = intensity_[i];
        const ImU32 base = touched_[i] ? cFloor : cEmpty;
        ImU32 fill = base;
        if (player_[i] && t > 0.0f) {
            fill = lerpColor(base, cPlayer, t);
        } else if (t > 0.0f) {
            const CatInfo& c = gTypes.cat(cat_[i]);
            fill = lerpColor(base, IM_COL32(c.r, c.g, c.b, 255), t);
        }
        dl->AddRectFilled(p0, p1, fill);

        if (player_[i] && t > 0.98f)
            dl->AddRect(p0, p1, cBorder, 0.0f, 0, 1.0f);
    }

    if (nextIndex_ >= 0 && nextIndex_ < n) {
        const int col = nextIndex_ % cols;
        const int row = nextIndex_ / cols;
        const ImVec2 p0(origin.x + col * step, origin.y + row * step);
        const ImVec2 p1(p0.x + s.cellSize, p0.y + s.cellSize);
        dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 255), 0.0f, 0, 1.0f);
    }
}
