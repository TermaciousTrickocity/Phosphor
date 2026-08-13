#include "overlay.hpp"

#include <cmath>

namespace overlay {

static void sub3(const float a[3], const float b[3], float o[3]) { o[0]=a[0]-b[0]; o[1]=a[1]-b[1]; o[2]=a[2]-b[2]; }
static float dot3(const float a[3], const float b[3]) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static void cross3(const float a[3], const float b[3], float o[3]) {
    o[0]=a[1]*b[2]-a[2]*b[1]; o[1]=a[2]*b[0]-a[0]*b[2]; o[2]=a[0]*b[1]-a[1]*b[0];
}
static void norm3(float v[3]) {
    float l = std::sqrt(dot3(v, v));
    if (l > 1e-8f) { v[0]/=l; v[1]/=l; v[2]/=l; }
}
static void mul4(const float a[16], const float b[16], float o[16]) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            o[r*4+c] = a[r*4+0]*b[0*4+c] + a[r*4+1]*b[1*4+c] + a[r*4+2]*b[2*4+c] + a[r*4+3]*b[3*4+c];
}

float horizToVertFov(float horizFovRadians, float w, float h) {
    if (w == 0.0f) return horizFovRadians;
    return 2.0f * std::atan(std::tan(horizFovRadians * 0.5f) * (h / w));
}

void buildViewProj(const float eye[3], const float fwd[3], const float up[3],
                   float vertFovRadians, float w, float h, float outVP[16]) {
    const float aspect = (h != 0.0f) ? (w / h) : 1.7777f;
    float vFov = vertFovRadians;
    if (vFov < 1e-4f) vFov = 1.0f;

    float z[3] = { fwd[0], fwd[1], fwd[2] }; norm3(z);
    float x[3]; cross3(up, z, x); norm3(x);
    float y[3]; cross3(z, x, y);
    x[0] = -x[0]; x[1] = -x[1]; x[2] = -x[2];
    const float view[16] = {
        x[0],          y[0],          z[0],          0.0f,
        x[1],          y[1],          z[1],          0.0f,
        x[2],          y[2],          z[2],          0.0f,
        -dot3(x, eye), -dot3(y, eye), -dot3(z, eye), 1.0f,
    };

    const float yScale = 1.0f / std::tan(vFov * 0.5f);
    const float xScale = yScale / aspect;
    const float zn = 0.01f, zf = 100000.0f;
    const float proj[16] = {
        xScale, 0.0f,   0.0f,                 0.0f,
        0.0f,   yScale, 0.0f,                 0.0f,
        0.0f,   0.0f,   zf / (zf - zn),       1.0f,
        0.0f,   0.0f,  -zn * zf / (zf - zn),  0.0f,
    };

    mul4(view, proj, outVP);
}

bool worldToScreen(const float vp[16], const float world[3],
                   float w, float h, float& sx, float& sy) {
    const float x = world[0], y = world[1], z = world[2];
    const float cx = x*vp[0] + y*vp[4] + z*vp[8]  + vp[12];
    const float cy = x*vp[1] + y*vp[5] + z*vp[9]  + vp[13];
    const float cw = x*vp[3] + y*vp[7] + z*vp[11] + vp[15];
    if (cw < 0.001f) return false;

    const float ndcX = cx / cw;
    const float ndcY = cy / cw;
    sx = (1.0f + ndcX) * 0.5f * w;
    sy = (1.0f - ndcY) * 0.5f * h;
    return true;
}

struct EnumCtx { unsigned long pid; HWND found; };

static BOOL CALLBACK enumProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid != ctx->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    if (GetWindowTextLengthW(hwnd) == 0) return TRUE;
    ctx->found = hwnd;
    return FALSE;
}

HWND findGameWindow(unsigned long pid) {
    EnumCtx ctx{ pid, nullptr };
    EnumWindows(enumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

static bool     g_saved = false;
static LONG_PTR g_style = 0;
static LONG_PTR g_exStyle = 0;
static RECT     g_rect = {};

void enterOverlay(HWND hwnd) {
    if (!g_saved) {
        g_style   = GetWindowLongPtrW(hwnd, GWL_STYLE);
        g_exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        GetWindowRect(hwnd, &g_rect);
        g_saved = true;
    }
    SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

void exitOverlay(HWND hwnd) {
    if (!g_saved) return;
    SetWindowLongPtrW(hwnd, GWL_STYLE, g_style);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, g_exStyle);
    SetWindowPos(hwnd, HWND_NOTOPMOST, g_rect.left, g_rect.top,
                 g_rect.right - g_rect.left, g_rect.bottom - g_rect.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    g_saved = false;
}

bool coverGameWindow(HWND overlayHwnd, HWND gameHwnd, float& outW, float& outH) {
    if (!gameHwnd || !IsWindow(gameHwnd)) return false;
    RECT cr;
    if (!GetClientRect(gameHwnd, &cr)) return false;
    POINT tl{ 0, 0 };
    ClientToScreen(gameHwnd, &tl);
    const int w = cr.right - cr.left;
    const int h = cr.bottom - cr.top;
    if (w <= 0 || h <= 0) return false;

    static bool have = false;
    static int  lx = 0, ly = 0, lw = 0, lh = 0;
    if (!have || tl.x != lx || tl.y != ly || w != lw || h != lh) {
        SetWindowPos(overlayHwnd, HWND_TOPMOST, tl.x, tl.y, w, h,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        lx = tl.x; ly = tl.y; lw = w; lh = h; have = true;
    }
    outW = float(w);
    outH = float(h);
    return true;
}

}
