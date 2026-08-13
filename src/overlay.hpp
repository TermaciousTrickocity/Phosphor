#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace overlay {

float horizToVertFov(float horizFovRadians, float w, float h);

void buildViewProj(const float eye[3], const float fwd[3], const float up[3],
                   float vertFovRadians, float w, float h, float outVP[16]);

bool worldToScreen(const float vp[16], const float world[3],
                   float w, float h, float& sx, float& sy);

HWND findGameWindow(unsigned long pid);

void enterOverlay(HWND hwnd);

void exitOverlay(HWND hwnd);

bool coverGameWindow(HWND overlayHwnd, HWND gameHwnd, float& outW, float& outH);

}
