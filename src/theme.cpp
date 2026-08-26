#include "theme.h"
#include "imgui_internal.h"
#include <cmath>
#include <string>
#include <unordered_map>

namespace theme {

static Palette g_pal{};
static int g_preset = 0;
static float g_hueShift = 0.0f;
static float g_time = 0.0f;
static bool g_palInit = false;
static ImFont* g_iconFont = nullptr;

void SetIconFont(ImFont* f) { g_iconFont = f; }

struct AccentSet { ImVec4 a; ImVec4 b; };
static const AccentSet kPresets[] = {
    { ImVec4(0.545f, 0.361f, 1.000f, 1.f), ImVec4(0.741f, 0.565f, 1.000f, 1.f) },
    { ImVec4(0.302f, 0.863f, 0.698f, 1.f), ImVec4(0.796f, 0.937f, 0.404f, 1.f) },
    { ImVec4(1.000f, 0.427f, 0.588f, 1.f), ImVec4(1.000f, 0.678f, 0.400f, 1.f) },
    { ImVec4(0.416f, 0.639f, 1.000f, 1.f), ImVec4(0.573f, 0.447f, 1.000f, 1.f) },
    { ImVec4(0.973f, 0.647f, 0.302f, 1.f), ImVec4(1.000f, 0.867f, 0.361f, 1.f) },
    { ImVec4(0.702f, 0.455f, 1.000f, 1.f), ImVec4(1.000f, 0.494f, 0.859f, 1.f) },
};

static void InitPalette() {
    if (g_palInit) return;
    g_palInit = true;
    g_pal.bg = ImVec4(0.051f, 0.055f, 0.080f, 1.f);
    g_pal.panel = ImVec4(0.086f, 0.092f, 0.133f, 1.f);
    g_pal.panelSoft = ImVec4(0.125f, 0.132f, 0.192f, 1.f);
    g_pal.border = ImVec4(1.f, 1.f, 1.f, 0.075f);
    g_pal.text = ImVec4(0.937f, 0.941f, 0.973f, 1.f);
    g_pal.dim = ImVec4(0.545f, 0.557f, 0.647f, 1.f);
    g_pal.valid = ImVec4(0.239f, 0.839f, 0.553f, 1.f);
    g_pal.guard = ImVec4(1.f, 0.706f, 0.329f, 1.f);
    g_pal.bad = ImVec4(1.f, 0.365f, 0.478f, 1.f);
    g_pal.warn = ImVec4(1.f, 0.776f, 0.353f, 1.f);
    SetAccentPreset(g_preset);
}

Palette& Pal() {
    InitPalette();
    return g_pal;
}

void SetAccentPreset(int idx) {
    if (idx < 0 || idx > 5) return;
    g_preset = idx;
    g_pal.accent = kPresets[idx].a;
    g_pal.accent2 = kPresets[idx].b;
}

int AccentPreset() { return g_preset; }

void Tick(float dt) {
    InitPalette();
    g_time += dt;
    float s = 0.5f + 0.5f * sinf(g_time * 0.25f);
    g_hueShift = s;
}

ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec4 AccentGlow(float alpha) {
    InitPalette();
    ImVec4 c = LerpColor(g_pal.accent, g_pal.accent2, g_hueShift);
    c.w = alpha;
    return c;
}

float Ease(float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return 1.0f - powf(1.0f - t, 3.0f);
}

float Approach(float cur, float target, float rate, float dt) {
    return cur + (target - cur) * (1.0f - expf(-rate * dt));
}

void GradientRounded(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx,
                     ImU32 c1, ImU32 c2, float r) {
    int n = 10;
    float w = mx.x - mn.x;
    ImVec4 a = ImGui::ColorConvertU32ToFloat4(c1);
    ImVec4 b = ImGui::ColorConvertU32ToFloat4(c2);
    for (int i = 0; i < n; i++) {
        float t0 = (float)i / n, t1 = (float)(i + 1) / n;
        ImU32 c = ImGui::GetColorU32(LerpColor(a, b, (t0 + t1) * 0.5f));
        int flags = 0;
        float x0 = mn.x + w * t0, x1 = mn.x + w * t1;
        if (i == 0) { flags = ImDrawFlags_RoundCornersLeft; }
        else if (i == n - 1) { flags = ImDrawFlags_RoundCornersRight; }
        else { x0 -= 0.5f; x1 += 0.5f; }
        dl->AddRectFilled(ImVec2(x0, mn.y), ImVec2(x1, mx.y), c, r, flags);
    }
}

void ApplyStyle() {
    InitPalette();
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = ImVec2(0, 0);
    st.FramePadding = ImVec2(12, 9);
    st.ItemSpacing = ImVec2(10, 10);
    st.ItemInnerSpacing = ImVec2(8, 6);
    st.CellPadding = ImVec2(8, 6);
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 0;
    st.PopupBorderSize = 0;
    st.FrameBorderSize = 0;
    st.WindowRounding = 0;
    st.ChildRounding = 14;
    st.FrameRounding = 9;
    st.PopupRounding = 12;
    st.ScrollbarSize = 8;
    st.ScrollbarRounding = 8;
    st.GrabRounding = 8;
    st.TabRounding = 8;
    st.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    st.SelectableTextAlign = ImVec2(0, 0.5f);

    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = g_pal.bg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = g_pal.panel;
    c[ImGuiCol_Text] = g_pal.text;
    c[ImGuiCol_TextDisabled] = g_pal.dim;
    c[ImGuiCol_FrameBg] = ImVec4(g_pal.panelSoft.x, g_pal.panelSoft.y, g_pal.panelSoft.z, 0.55f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(g_pal.panelSoft.x, g_pal.panelSoft.y, g_pal.panelSoft.z, 0.85f);
    c[ImGuiCol_FrameBgActive] = ImVec4(g_pal.panelSoft.x, g_pal.panelSoft.y, g_pal.panelSoft.z, 1.f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(g_pal.panel.x, g_pal.panel.y, g_pal.panel.z, 0.45f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(g_pal.accent.x, g_pal.accent.y, g_pal.accent.z, 0.55f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(g_pal.accent.x, g_pal.accent.y, g_pal.accent.z, 0.85f);
    c[ImGuiCol_ScrollbarGrabActive] = g_pal.accent;
    c[ImGuiCol_Button] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ButtonHovered] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ButtonActive] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Header] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_HeaderHovered] = AccentGlow(0.14f);
    c[ImGuiCol_HeaderActive] = AccentGlow(0.22f);
    c[ImGuiCol_CheckMark] = AccentGlow(1.f);
    c[ImGuiCol_SliderGrab] = AccentGlow(1.f);
    c[ImGuiCol_SliderGrabActive] = g_pal.accent2;
    c[ImGuiCol_TextSelectedBg] = AccentGlow(0.35f);
    c[ImGuiCol_NavCursor] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TitleBg] = g_pal.bg;
    c[ImGuiCol_Border] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
}

void GradientRectV(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float rad,
                   ImU32 top, ImU32 bottom) {
    int n = 14;
    float h = (mx.y - mn.y) / (float)n;
    for (int i = 0; i < n; i++) {
        float t0 = (float)i / (float)(n - 1);
        ImU32 c = ImGui::GetColorU32(LerpColor(
            ImGui::ColorConvertU32ToFloat4(top),
            ImGui::ColorConvertU32ToFloat4(bottom), t0));
        ImVec2 a(mn.x, mn.y + h * i - (i ? 0.5f : 0.f));
        ImVec2 b(mx.x, mn.y + h * (i + 1));
        ImDrawFlags f = ImDrawFlags_RoundCornersNone;
        if (i == 0) f = ImDrawFlags_RoundCornersTop;
        if (i == n - 1) f = ImDrawFlags_RoundCornersBottom;
        dl->AddRectFilled(a, b, c, rad, f);
    }
}

bool GlowButton(const char* id, const char* label, const ImVec2& size, bool filled) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 sz = size.x <= 0 || size.y <= 0
                    ? ImGui::CalcItemSize(size, ImGui::GetContentRegionAvail().x,
                                          ImGui::GetFrameHeight())
                    : size;
    bool pressed = ImGui::InvisibleButton(id, sz, ImGuiButtonFlags_PressedOnClick);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiID key = ImGui::GetID(id);
    float* hovp = ImGui::GetStateStorage()->GetFloatRef(key + 1, 0.f);
    *hovp = Approach(*hovp, ImGui::IsItemHovered() ? 1.f : 0.f, 12.f, ImGui::GetIO().DeltaTime);
    float hov = *hovp;

    ImVec2 mn = ImVec2(pos.x + (1 - hov) * 1.0f, pos.y);
    ImVec2 mx = ImVec2(pos.x + sz.x, pos.y + sz.y);
    float rad = 10.f;

    if (filled) {
        dl->AddRectFilled(mn, mx, ImGui::GetColorU32(g_pal.accent), rad);
        if (hov > 0.01f)
            dl->AddRect(mn, mx, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.35f * hov)), rad, 0, 1.4f);
        dl->AddRectFilled(mn, mx, IM_COL32(255, 255, 255, (int)(18 * hov)), rad);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(18, 14, 30, 255));
    } else {
        dl->AddRectFilled(mn, mx, ImGui::GetColorU32(
            ImVec4(g_pal.panelSoft.x, g_pal.panelSoft.y, g_pal.panelSoft.z, 0.5f + 0.3f * hov)), rad);
        dl->AddRect(mn, mx, ImGui::GetColorU32(
            ImVec4(g_pal.border.x, g_pal.border.y, g_pal.border.z,
                   g_pal.border.w + 0.25f * hov)), rad, 0, 1.2f);
    }

    ImVec2 tsz = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(pos.x + (sz.x - tsz.x) * 0.5f, pos.y + (sz.y - tsz.y) * 0.5f),
                ImGui::GetColorU32(filled ? ImGuiCol_Text : ImGuiCol_Text), label);
    if (filled) ImGui::PopStyleColor();

    return pressed;
}

bool IconButton(const char* id, const char* icon, float size, ImU32 tint) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float hov = Approach(ImGui::GetStateStorage()->GetFloat(ImGui::GetID(id), 0),
                         ImGui::IsItemHovered() ? 1.f : 0.f, 14.f, ImGui::GetIO().DeltaTime);
    ImGui::GetStateStorage()->SetFloat(ImGui::GetID(id), hov);

    ImVec2 c = ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f);
    if (hov > 0.01f)
        dl->AddCircleFilled(c, size * 0.46f, ImGui::GetColorU32(AccentGlow(0.16f * hov)));

    ImFont* f = g_iconFont ? g_iconFont : ImGui::GetFont();
    ImVec2 tsz = f->CalcTextSizeA(size * 0.62f, FLT_MAX, 0, icon);
    dl->AddText(f, size * 0.62f, ImVec2(c.x - tsz.x * 0.5f, c.y - tsz.y * 0.5f),
                tint ? tint : ImGui::GetColorU32(ImVec4(g_pal.text.x, g_pal.text.y,
                                                        g_pal.text.z, 0.75f + 0.25f * hov)),
                icon);
    return pressed;
}

bool Toggle(const char* id, bool* v) {
    float h = ImGui::GetFrameHeight() * 0.72f;
    float w = h * 1.75f;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGuiID key = ImGui::GetID(id);
    ImGui::InvisibleButton(id, ImVec2(w, h));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) *v = !*v;

    float* anim = ImGui::GetStateStorage()->GetFloatRef(key + 1, 0.f);
    *anim = Approach(*anim, *v ? 1.f : 0.f, 14.f, ImGui::GetIO().DeltaTime);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mx = ImVec2(pos.x + w, pos.y + h);
    ImU32 bgc = ImGui::GetColorU32(
        ImVec4(g_pal.accent.x * (*anim) + g_pal.panelSoft.x * (1 - *anim),
               g_pal.accent.y * (*anim) + g_pal.panelSoft.y * (1 - *anim),
               g_pal.accent.z * (*anim) + g_pal.panelSoft.z * (1 - *anim),
               0.35f + 0.65f * (*anim)));
    dl->AddRectFilled(pos, mx, bgc, h * 0.5f);
    float kx = pos.x + h * 0.5f + (*anim) * (w - h);
    dl->AddCircleFilled(ImVec2(kx, pos.y + h * 0.5f), h * 0.38f,
                        IM_COL32(240, 242, 250, 255));
    return *v;
}

void Spinner(const ImVec2& center, float radius, float thickness, ImU32 color, float speed) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float phase = fmodf(g_time * speed, 1.0f);
    float start = phase * IM_PI * 2.0f;
    float arc = IM_PI * (0.55f + 0.45f * sinf(g_time * 2.2f));
    dl->PathClear();
    for (int i = 0; i <= 28; i++) {
        float a = start + arc * ((float)i / 28.0f);
        dl->PathLineTo(ImVec2(center.x + cosf(a) * radius, center.y + sinf(a) * radius));
    }
    dl->PathStroke(color, 0, thickness);
}

void Badge(const char* text, ImU32 color) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 tsz = ImGui::CalcTextSize(text);
    float padX = 8, padY = 3;
    ImVec2 mx = ImVec2(p.x + tsz.x + padX * 2, p.y + tsz.y + padY * 2);
    ImVec4 cv = ImGui::ColorConvertU32ToFloat4(color);
    dl->AddRectFilled(p, mx, ImGui::GetColorU32(ImVec4(cv.x, cv.y, cv.z, 0.16f)), 7);
    dl->AddRect(p, mx, ImGui::GetColorU32(ImVec4(cv.x, cv.y, cv.z, 0.55f)), 7, 0, 1.1f);
    dl->AddText(ImVec2(p.x + padX, p.y + padY), color, text);
    ImGui::Dummy(ImVec2(mx.x - p.x, mx.y - p.y));
}

void Card(const char* id, const ImVec2& size, ImU32 bg) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 sz = ImGui::CalcItemSize(size, ImGui::GetContentRegionAvail().x, size.y);
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), bg, 13);
    ImGui::BeginChild(id, sz, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground |
                          ImGuiWindowFlags_NoMove);
    ImGui::SetCursorPos(ImVec2(14, 12));
    ImGui::Dummy(ImVec2(sz.x - 28, sz.y - 24));
    ImGui::EndChild();
}

}
