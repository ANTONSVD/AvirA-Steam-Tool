#pragma once
#include <windows.h>
#include "imgui.h"

namespace app {

void Init();
void Render();
void Shutdown();

extern HWND g_hwnd;
extern ImTextureID g_logo;
extern ImFont* g_fontMain;
extern ImFont* g_fontBold;
extern ImFont* g_fontTitle;
extern ImFont* g_fontMono;
extern ImFont* g_fontIcon;

}
