#pragma once
#include "imgui.h"

namespace theme {

struct Palette {
    ImVec4 bg;
    ImVec4 panel;
    ImVec4 panelSoft;
    ImVec4 border;
    ImVec4 text;
    ImVec4 dim;
    ImVec4 accent;
    ImVec4 accent2;
    ImVec4 valid;
    ImVec4 guard;
    ImVec4 bad;
    ImVec4 warn;
};

Palette& Pal();
void SetAccentPreset(int idx);
int AccentPreset();
void Tick(float dt);
ImVec4 AccentGlow(float alpha);

void ApplyStyle();
void SetIconFont(ImFont* f);
void GradientRounded(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx,
                     ImU32 c1, ImU32 c2, float r);
float Ease(float t);
float Approach(float cur, float target, float rate, float dt);

bool GlowButton(const char* id, const char* label, const ImVec2& size, bool filled);
void GradientRectV(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float rad,
                   ImU32 top, ImU32 bottom);
bool IconButton(const char* id, const char* icon, float size, ImU32 tint = 0);
bool Toggle(const char* id, bool* v);
void Spinner(const ImVec2& center, float radius, float thickness, ImU32 color, float speed);
void Badge(const char* text, ImU32 color);
void Card(const char* id, const ImVec2& size, ImU32 bg);

}
