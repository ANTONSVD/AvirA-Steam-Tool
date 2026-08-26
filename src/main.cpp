#include "app.h"
#include "theme.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                                             WPARAM wParam, LPARAM lParam);

static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static IDXGISwapChain* g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;

static bool CreateDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl;
    D3D_FEATURE_LEVEL fls[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                               fls, 2, D3D11_SDK_VERSION, &sd, &g_swap,
                                               &g_dev, &fl, &g_ctx);
    if (FAILED(hr)) return false;

    ID3D11Texture2D* bb = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&bb));
    g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
    bb->Release();
    return true;
}

static void CleanupDevice() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
}

static ID3D11ShaderResourceView* LoadTextureWIC(const wchar_t* path) {
    IWICImagingFactory* fac = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    do {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac))))
            break;
        if (FAILED(fac->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnLoad, &dec)))
            break;
        if (FAILED(dec->GetFrame(0, &frame)))
            break;
        if (FAILED(fac->CreateFormatConverter(&conv)))
            break;
        if (FAILED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                    WICBitmapDitherTypeNone, nullptr, 0,
                                    WICBitmapPaletteTypeCustom)))
            break;
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        if (!w || !h) break;
        std::vector<unsigned char> buf((size_t)w * h * 4);
        if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)buf.size(), buf.data())))
            break;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{buf.data(), w * 4, 0};
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(g_dev->CreateTexture2D(&td, &sd, &tex))) {
            g_dev->CreateShaderResourceView(tex, nullptr, &srv);
            tex->Release();
        }
    } while (false);
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    if (fac) fac->Release();
    return srv;
}

static ImTextureID LoadLogo() {
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir = exe;
    size_t p = dir.find_last_of("\\/");
    if (p != std::string::npos) dir = dir.substr(0, p);

    const char* candidates[] = {
        "AvirA Logo.png",
        "..\\..\\..\\AvirA Logo.png",
        "..\\..\\..\\..\\AvirA Logo.png",
    };
    for (auto* rel : candidates) {
        std::string full = dir + "\\" + rel;
        DWORD attr = GetFileAttributesA(full.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
        int n = MultiByteToWideChar(CP_UTF8, 0, full.c_str(), -1, nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, full.c_str(), -1, w.data(), n);
        w.resize(n ? n - 1 : 0);
        ID3D11ShaderResourceView* srv = LoadTextureWIC(w.c_str());
        if (srv) return (ImTextureID)(intptr_t)srv;
    }
    return (ImTextureID)0;
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
        case WM_SIZE:
            if (g_dev && wParam != SIZE_MINIMIZED) {
                if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
                g_swap->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam),
                                      DXGI_FORMAT_UNKNOWN, 0);
                ID3D11Texture2D* bb = nullptr;
                g_swap->GetBuffer(0, IID_PPV_ARGS(&bb));
                g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
                bb->Release();
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static ImFont* LoadFont(const char* file, float size, const ImWchar* ranges) {
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(file, size, nullptr, ranges);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"AviraSteamTool";
    RegisterClassExW(&wc);

    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);

    HWND hwnd = CreateWindowExW(0, L"AviraSteamTool", L"AvirA Steam Tool",
                                WS_POPUP | WS_MINIMIZEBOX | WS_CLIPSIBLINGS |
                                    WS_CLIPCHILDREN,
                                (sx - 1180) / 2, (sy - 760) / 2, 1180, 760,
                                nullptr, nullptr, hInst, nullptr);
    app::g_hwnd = hwnd;

    BOOL round = TRUE;
    DwmSetWindowAttribute(hwnd, 33, &round, sizeof(round));

    if (!CreateDevice(hwnd)) return 1;
    app::g_logo = LoadLogo();

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    static const ImWchar cyr[] = {0x0020, 0x00FF, 0x0400, 0x04FF,
                                  0x2010, 0x205E, 0x2022, 0x2022, 0};
    static const ImWchar mdl2[] = {0xE700, 0xE900, 0};

    app::g_fontMain = LoadFont("C:\\Windows\\Fonts\\segoeui.ttf", 17.0f, cyr);
    app::g_fontBold = LoadFont("C:\\Windows\\Fonts\\segoeuib.ttf", 18.0f, cyr);
    app::g_fontTitle = LoadFont("C:\\Windows\\Fonts\\segoeuib.ttf", 24.0f, cyr);
    app::g_fontMono = LoadFont("C:\\Windows\\Fonts\\consola.ttf", 16.0f, cyr);
    app::g_fontIcon = LoadFont("C:\\Windows\\Fonts\\SegMDL2.ttf", 19.0f, mdl2);
    if (!app::g_fontMain) app::g_fontMain = io.Fonts->AddFontDefault();

    theme::SetIconFont(app::g_fontIcon);
    theme::ApplyStyle();
    app::Init();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    MSG msg{};
    ZeroMemory(&msg, sizeof(msg));
    bool done = false;
    while (!done) {
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        app::Render();

        ImGui::Render();
        const float clear[4] = {0.02f, 0.024f, 0.04f, 1.f};
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_swap->Present(1, 0);
    }

    app::Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(L"AviraSteamTool", hInst);
    return 0;
}
