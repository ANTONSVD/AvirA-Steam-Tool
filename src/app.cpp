#include "app.h"
#include "theme.h"
#include "checker.h"
#include "accounts.h"
#include "steamctl.h"
#include "util.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include <mutex>
#include <thread>
#include <atomic>
#include <deque>
#include <cmath>
#include <commdlg.h>

namespace app {

HWND g_hwnd = nullptr;
ImTextureID g_logo = (ImTextureID)0;
ImFont* g_fontMain = nullptr;
ImFont* g_fontBold = nullptr;
ImFont* g_fontTitle = nullptr;
ImFont* g_fontMono = nullptr;
ImFont* g_fontIcon = nullptr;

static const int kWinW = 1180;
static const int kWinH = 760;

enum Page { PageChecker = 0, PageAccounts, PageSettings };

struct FeedItem {
    AccStatus st;
    std::string user;
    std::string msg;
    long long born;
};

struct ToastItem {
    int type;
    std::string text;
    long long born;
};

struct EvResult {
    Cred cred;
    CheckOutcome oc;
};

static struct {
    AccountStore store;
    Checker checker;
    std::string comboText;
    std::vector<std::string> proxies;
    int threads = 8;
    int page = PageChecker;
    float pageAnim = 1.0f;
    float pageDir = 1.0f;
    float intro = 0.0f;
    bool maskPass = true;
    bool soundHit = true;
    bool autoExport = true;
    bool skipKnown = true;
    bool sortByNew = true;
    int feedFilter = 0;
    char search[96] = {0};
    std::vector<FeedItem> feed;
    std::deque<ToastItem> toasts;
    std::deque<EvResult> events;
    std::mutex mtx;
    std::atomic<bool> loginBusy{false};
    std::string loggingUser;
    std::string steamPath;
    std::string currentUser;
    bool steamRunning = false;
    long long lastSteamCheck = 0;
    float dispValid = 0, dispGuard = 0, dispBad = 0, dispErr = 0;
    float navY[3] = {0, 0, 0};
    float navHover[3] = {0, 0, 0};
    ImVec2 winPos{}, winSize{};
    std::string dataDir;
    int accountCount = 0;
} S;

static ImU32 StatusColor(AccStatus st) {
    auto& p = theme::Pal();
    switch (st) {
        case AccStatus::Valid: return ImGui::GetColorU32(p.valid);
        case AccStatus::Guard: return ImGui::GetColorU32(p.guard);
        case AccStatus::Invalid: return ImGui::GetColorU32(p.bad);
        default: return ImGui::GetColorU32(p.warn);
    }
}

static std::string Ic(unsigned cp) {
    std::string s;
    s += (char)(0xE0 | (cp >> 12));
    s += (char)(0x80 | ((cp >> 6) & 0x3F));
    s += (char)(0x80 | (cp & 0x3F));
    return s;
}

static const std::string& IconNav(int i) {
    static std::string icons[3] = {Ic(0xE945), Ic(0xE716), Ic(0xE713)};
    return icons[i];
}

struct TipState {
    bool active = false;
    std::string text;
    ImVec2 mn{}, mx{};
};
static TipState g_tip;

static void Tooltip(const char* txt) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) return;
    g_tip.active = true;
    g_tip.text = txt;
    g_tip.mn = ImGui::GetItemRectMin();
    g_tip.mx = ImGui::GetItemRectMax();
}

static ImU32 RowBg();
static ImU32 AccentHoverRow();
static const ImVec4* kPresetsA();
static const ImVec4* kPresetsB();
static std::string g_icPlay, g_icStop, g_icFolder, g_icCopy, g_icTrash, g_icRefresh,
    g_icSearch, g_icClose, g_icMin, g_icSave, g_icLock, g_icBolt;

static void PushToast(int type, const std::string& text) {
    std::lock_guard<std::mutex> l(S.mtx);
    if (S.toasts.size() > 5) S.toasts.pop_front();
    S.toasts.push_back({type, text, util::NowMs()});
}

static void SaveSettings() {
    std::string ini = "threads=" + std::to_string(S.threads) +
                      "\npreset=" + std::to_string(theme::AccentPreset()) +
                      "\nsound=" + std::to_string(S.soundHit ? 1 : 0) +
                      "\nmask=" + std::to_string(S.maskPass ? 1 : 0) +
                      "\nautoexport=" + std::to_string(S.autoExport ? 1 : 0) +
                      "\nskipknown=" + std::to_string(S.skipKnown ? 1 : 0) + "\n";
    util::WriteTextFile(S.dataDir + "\\settings.ini", ini);
}

static void LoadSettings() {
    std::string ini;
    if (!util::ReadTextFile(S.dataDir + "\\settings.ini", ini)) return;
    for (auto& line : util::SplitLines(ini)) {
        size_t p = line.find('=');
        if (p == std::string::npos) continue;
        std::string k = util::Trim(line.substr(0, p));
        int v = atoi(util::Trim(line.substr(p + 1)).c_str());
        if (k == "threads") S.threads = v < 1 ? 1 : (v > 64 ? 64 : v);
        else if (k == "preset") theme::SetAccentPreset(v);
        else if (k == "sound") S.soundHit = v != 0;
        else if (k == "mask") S.maskPass = v != 0;
        else if (k == "autoexport") S.autoExport = v != 0;
        else if (k == "skipknown") S.skipKnown = v != 0;
    }
}

static bool OpenFileDialog(std::string& outPath) {
    char buf[MAX_PATH] = {0};
    OPENFILENAMEA of{};
    of.lStructSize = sizeof(of);
    of.hwndOwner = g_hwnd;
    of.lpstrFilter = "Text files (*.txt)\0*.txt\0All files\0*.*\0";
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameA(&of)) return false;
    outPath = buf;
    return true;
}

static void LoadComboFile() {
    std::string path;
    if (!OpenFileDialog(path)) return;
    std::string data;
    if (!util::ReadTextFile(path, data)) {
        PushToast(3, "Не удалось открыть файл");
        return;
    }
    S.comboText = data;
    PushToast(0, "Загружено аккаунтов: " + std::to_string(ParseCombos(data).size()));
}

static void LoadProxyFile() {
    std::string path;
    if (!OpenFileDialog(path)) return;
    std::string data;
    if (!util::ReadTextFile(path, data)) return;
    S.proxies.clear();
    for (auto& l : util::SplitLines(data)) {
        std::string p = util::Trim(l);
        if (!p.empty() && p[0] != '#') S.proxies.push_back(p);
    }
    util::WriteTextFile(S.dataDir + "\\proxies.txt", data);
    PushToast(0, "Прокси загружены: " + std::to_string(S.proxies.size()));
}

static void StartChecker() {
    if (S.checker.Running()) return;
    auto combos = ParseCombos(S.comboText);
    if (combos.empty()) {
        PushToast(2, "Добавьте аккаунты: login:password");
        return;
    }
    if (S.skipKnown) {
        std::vector<std::string> known;
        for (auto& a : S.store.Items())
            known.push_back(a.user);
        S.checker.SetKnownAccounts(known);
    }
    S.checker.SetSkipKnown(S.skipKnown);
    int totalIn = (int)combos.size();
    S.checker.Start(combos, S.proxies, S.threads);
    int skipped = totalIn - S.checker.Total();
    if (skipped > 0)
        PushToast(0, "Пропущено известных: " + std::to_string(skipped));
}

static void StopChecker() {
    S.checker.Stop();
    PushToast(0, "Проверка остановлена");
}

static void TogglePause() {
    if (!S.checker.Running()) return;
    if (S.checker.Paused()) {
        S.checker.Resume();
        PushToast(0, "Продолжено");
    } else {
        S.checker.Pause();
        PushToast(0, "Пауза · Space — продолжить");
    }
}

static void OnCheckerEvent(const Cred& c, const CheckOutcome& oc) {
    std::lock_guard<std::mutex> l(S.mtx);
    S.events.push_back({c, oc});
}

static void DrainEvents() {
    std::deque<EvResult> evs;
    {
        std::lock_guard<std::mutex> l(S.mtx);
        evs.swap(S.events);
    }
    for (auto& e : evs) {
        {
            std::lock_guard<std::mutex> l(S.mtx);
            if (S.feed.size() > 250) S.feed.erase(S.feed.begin());
            S.feed.push_back({e.oc.status, e.cred.user, e.oc.message, util::NowMs()});
        }
        if (e.oc.status == AccStatus::Valid || e.oc.status == AccStatus::Guard) {
            S.store.AddOrUpdate(e.cred, e.oc.status);
            S.store.Save(S.dataDir + "\\accounts.txt");
            if (S.autoExport)
                util::AppendTextFile(S.dataDir + "\\hits.txt",
                                     e.cred.user + ":" + e.cred.pass);
        }
        if (e.oc.status == AccStatus::Valid) {
            if (S.soundHit) MessageBeep(MB_ICONASTERISK);
            PushToast(1, "VALID · " + e.cred.user);
        } else if (e.oc.status == AccStatus::Guard) {
            PushToast(2, "2FA GUARD · " + e.cred.user);
        }
    }
    if (S.checker.Running() && S.checker.Total() > 0 &&
        S.checker.Done() >= S.checker.Total()) {
        S.checker.Stop();
        std::string sum = "Проверка завершена · " +
                          std::to_string(S.checker.Hits()) + " valid / " +
                          std::to_string(S.checker.Guards()) + " 2fa";
        PushToast(1, sum);
    }
}

static void RefreshSteamInfo() {
    S.steamRunning = IsSteamRunning();
    S.currentUser = GetSteamAutoLoginUser();
    S.lastSteamCheck = util::NowMs();
}

static void DoLoginAsync(std::string user, std::string pass) {
    if (S.loginBusy.exchange(true)) return;
    S.loggingUser = user;
    std::thread([user, pass]() {
        SteamLoginResult r = LoginToAccount(user, pass);
        switch (r) {
            case SteamLoginResult::Started:
                PushToast(1, "Выполняется вход · " + user); break;
            case SteamLoginResult::Restarted:
                PushToast(1, "Steam перезапущен на " + user); break;
            case SteamLoginResult::AlreadyActive:
                PushToast(0, "Этот аккаунт уже активен"); break;
            case SteamLoginResult::NoSteam:
                PushToast(3, "Steam не найден в системе"); break;
            default:
                PushToast(3, "Не удалось запустить Steam"); break;
        }
        RefreshSteamInfo();
        S.loggingUser.clear();
        S.loginBusy = false;
    }).detach();
}

void Init() {
    S.dataDir = util::ExeDir() + "\\data";
    CreateDirectoryA(S.dataDir.c_str(), nullptr);
    LoadSettings();
    S.store.Load(S.dataDir + "\\accounts.txt");

    g_icPlay = Ic(0xE768);
    g_icStop = Ic(0xE71A);
    g_icFolder = Ic(0xE8E5);
    g_icCopy = Ic(0xE8C8);
    g_icTrash = Ic(0xE74D);
    g_icRefresh = Ic(0xE72C);
    g_icSearch = Ic(0xE721);
    g_icClose = Ic(0xE8BB);
    g_icMin = Ic(0xE921);
    g_icSave = Ic(0xE78C);
    g_icLock = Ic(0xE72E);

    std::string px;
    if (util::ReadTextFile(S.dataDir + "\\proxies.txt", px)) {
        for (auto& l : util::SplitLines(px)) {
            std::string p = util::Trim(l);
            if (!p.empty() && p[0] != '#') S.proxies.push_back(p);
        }
    }
    S.checker.SetCallback(OnCheckerEvent);
    S.steamPath = GetSteamPath();
    RefreshSteamInfo();
}

void Shutdown() {
    S.checker.Stop();
    SaveSettings();
    S.store.Save(S.dataDir + "\\accounts.txt");
}

static float EaseBack(float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    float c = 1.70158f;
    float u = t - 1;
    return 1 + (c + 1) * u * u * u + c * u * u;
}

static void DrawBackground() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 sz = ImGui::GetWindowSize();
    dl->AddRectFilledMultiColor(S.winPos, ImVec2(S.winPos.x + sz.x, S.winPos.y + sz.y),
                                IM_COL32(0, 0, 0, 60), IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 100));
}

static bool ChromeBtn(const char* id, int kind) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 sz(36, 30);
    bool pressed = ImGui::InvisibleButton(id, sz);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool hov = ImGui::IsItemHovered();
    if (hov) {
        ImU32 bg = kind == 1 ? IM_COL32(232, 17, 35, 210) : IM_COL32(255, 255, 255, 24);
        dl->AddRectFilled(pos, ImVec2(pos.x + sz.x, pos.y + sz.y), bg, 8);
    }
    ImVec2 c(pos.x + sz.x * 0.5f, pos.y + sz.y * 0.5f);
    ImU32 col = (hov && kind == 1) ? IM_COL32(255, 255, 255, 255)
                                   : ImGui::GetColorU32(theme::Pal().dim);
    if (kind == 0) {
        dl->AddLine(ImVec2(c.x - 5, c.y), ImVec2(c.x + 5, c.y), col, 1.6f);
    } else {
        dl->AddLine(ImVec2(c.x - 4.5f, c.y - 4.5f), ImVec2(c.x + 4.5f, c.y + 4.5f), col, 1.6f);
        dl->AddLine(ImVec2(c.x + 4.5f, c.y - 4.5f), ImVec2(c.x - 4.5f, c.y + 4.5f), col, 1.6f);
    }
    return pressed;
}

static void DrawTitlebar(float w) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 org = ImGui::GetCursorScreenPos();
    float h = 52.0f;

    if (g_logo) {
        dl->AddImageRounded(g_logo, ImVec2(org.x + 14, org.y + 11),
                            ImVec2(org.x + 44, org.y + 41), ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32_WHITE, 9);
        ImGui::SetCursorScreenPos(ImVec2(org.x + 14, org.y + 11));
        ImGui::InvisibleButton("##logo", ImVec2(30, 30));
        Tooltip("AvirA Steam Toolkit  v1.0");
    } else {
        ImVec2 c(org.x + 30, org.y + h * 0.5f);
        float r = 9.5f;
        ImU32 c1 = ImGui::GetColorU32(theme::Pal().accent);
        ImU32 c2 = ImGui::GetColorU32(theme::Pal().accent2);
        ImVec2 pts[4];
        for (int i = 0; i < 4; i++) {
            float a = 0.785398f + i * 1.570796f;
            pts[i] = ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r);
        }
        dl->AddTriangleFilled(pts[0], pts[1], pts[2], c1);
        dl->AddTriangleFilled(pts[0], pts[2], pts[3], c2);
    }

    ImGui::SetCursorScreenPos(ImVec2(org.x + 56, org.y + 14));
    ImGui::PushFont(g_fontTitle);
    dl->AddText(ImGui::GetCursorScreenPos(), ImGui::GetColorU32(theme::Pal().text), "AvirA");
    ImVec2 tsz = ImGui::CalcTextSize("AvirA");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(org.x + 56 + tsz.x + 12, org.y + 19));
    dl->AddText(ImGui::GetCursorScreenPos(),
                ImGui::GetColorU32(theme::Pal().dim), "steam toolkit");

    if (S.checker.Running()) {
        float stX = org.x + w - 240;
        const char* stText = S.checker.Paused() ? "PAUSED" : "SCANNING";
        ImVec4 stCol = S.checker.Paused() ? theme::Pal().guard : theme::Pal().valid;
        ImVec2 stz = ImGui::CalcTextSize(stText);
        dl->AddText(ImVec2(stX, org.y + h * 0.5f - stz.y * 0.5f),
                    ImGui::GetColorU32(stCol), stText);
        char stn[32];
        snprintf(stn, sizeof(stn), "%d/%d", S.checker.Done(), S.checker.Total());
        ImVec2 snz = ImGui::CalcTextSize(stn);
        dl->AddText(ImVec2(stX + stz.x + 10, org.y + h * 0.5f - snz.y * 0.5f),
                    ImGui::GetColorU32(theme::Pal().dim), stn);
    }

    ImGui::SetCursorScreenPos(ImVec2(org.x + w - 84, org.y + 11));
    bool minB = ChromeBtn("##min", 0);
    Tooltip("Свернуть");
    ImGui::SameLine(0, 2);
    bool closeB = ChromeBtn("##close", 1);
    Tooltip("Закрыть");
    if (minB) ShowWindow(g_hwnd, SW_MINIMIZE);
    if (closeB) PostMessageA(g_hwnd, WM_CLOSE, 0, 0);

    ImGui::SetCursorScreenPos(org);
    ImGui::InvisibleButton("##drag", ImVec2(w - 90, h - 8),
                           ImGuiButtonFlags_MouseButtonLeft);
    if (ImGui::IsItemActive()) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        if (d.x != 0 || d.y != 0) {
            RECT rc;
            GetWindowRect(g_hwnd, &rc);
            SetWindowPos(g_hwnd, nullptr, (int)(rc.left + d.x), (int)(rc.top + d.y),
                         0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(org.x, org.y + h));
}

struct NavState {
    float pillY = 0;
    bool init = false;
};
static NavState g_nav;

static bool NavItem(int idx, const char* label, float w) {
    bool clicked = false;
    float ih = 46.0f;
    ImVec2 org = ImGui::GetCursorScreenPos();
    auto& p = theme::Pal();

    S.navHover[idx] = theme::Approach(S.navHover[idx],
                                      ImGui::IsWindowHovered() &&
                                              ImGui::IsMouseHoveringRect(org, ImVec2(org.x + w, org.y + ih))
                                          ? 1.f
                                          : 0.f,
                                      10.f, ImGui::GetIO().DeltaTime);

    if (ImGui::InvisibleButton(label, ImVec2(w - 20, ih))) clicked = true;

    if (S.page == idx && !g_nav.init) g_nav.pillY = org.y;

    if (S.navHover[idx] > 0.01f || S.page == idx) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(org.x + 6, org.y + 3), ImVec2(org.x + w - 14, org.y + ih - 3),
            ImGui::GetColorU32(theme::AccentGlow(0.10f * S.navHover[idx] +
                                                 (S.page == idx ? 0.10f : 0.f))),
            11);
    }

    if (S.page == idx) {
        g_nav.pillY = theme::Approach(g_nav.pillY, org.y, 16.f, ImGui::GetIO().DeltaTime);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(org.x + 6, g_nav.pillY + 6),
                          ImVec2(org.x + 9, g_nav.pillY + ih - 6),
                          ImGui::GetColorU32(theme::AccentGlow(0.95f)), 2);
        dl->AddRectFilled(ImVec2(org.x + 6, g_nav.pillY + 6),
                          ImVec2(org.x + w - 14, g_nav.pillY + ih - 6),
                          ImGui::GetColorU32(theme::AccentGlow(0.08f)), 11);
    }

    ImFont* iconFont = g_fontIcon ? g_fontIcon : g_fontMain;
    ImVec2 ipos(org.x + 26, org.y + ih * 0.5f - 9);
    ImU32 tint = ImGui::GetColorU32(S.page == idx
                                        ? theme::Pal().text
                                        : ImVec4(p.dim.x, p.dim.y, p.dim.z,
                                                 0.55f + 0.45f * S.navHover[idx]));
    ImGui::GetWindowDrawList()->AddText(iconFont, 18.0f, ipos, tint,
                                        IconNav(idx).c_str());

    ImVec2 tpos(org.x + 56, org.y + ih * 0.5f - 8.5f);
    ImGui::GetWindowDrawList()->AddText(tpos, tint, label);
    return clicked;
}

static void StatCard(const char* id, const char* label, float disp, int real,
                     ImU32 color, float share, float cardW) {
    (void)id; (void)share;
    ImVec2 sz(cardW, 96);
    ImVec2 org = ImGui::GetCursorScreenPos();
    auto& p = theme::Pal();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(org, ImVec2(org.x + sz.x, org.y + sz.y),
                      ImGui::GetColorU32(ImVec4(p.panelSoft.x, p.panelSoft.y,
                                                p.panelSoft.z, 0.42f)), 14);
    dl->AddRect(org, ImVec2(org.x + sz.x, org.y + sz.y),
                ImGui::GetColorU32(ImVec4(p.border.x, p.border.y, p.border.z,
                                          p.border.w)), 14, 0, 1.1f);

    ImGui::SetCursorScreenPos(ImVec2(org.x + 18, org.y + 16));
    dl->AddText(ImVec2(org.x + 18, org.y + 16), ImGui::GetColorU32(p.dim), label);
    ImGui::PushFont(g_fontTitle);
    char num[16];
    snprintf(num, sizeof(num), "%d", (int)(disp + 0.5f));
    ImGui::SetCursorScreenPos(ImVec2(org.x + 18, org.y + 36));
    dl->AddText(g_fontTitle, 27.f, ImGui::GetCursorScreenPos(), color, num);
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(org);
    ImGui::Dummy(sz);
}

static void ProgressPulse(const char* label, ImVec2 mn, ImVec2 mx) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = (mx.y - mn.y) * 0.5f;
    dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.06f)), r);
    float pct = S.checker.Total() > 0
                    ? (float)S.checker.Done() / (float)S.checker.Total()
                    : 0.f;
    if (pct > 0.003f) {
        ImVec2 f1 = mn, f2 = ImVec2(mn.x + (mx.x - mn.x) * pct, mx.y);
        int flags = pct > 0.985f ? ImDrawFlags_RoundCornersAll
                                : ImDrawFlags_RoundCornersLeft;
        dl->AddRectFilled(f1, f2, ImGui::GetColorU32(theme::Pal().accent), r, flags);
    }
    float t = fmodf((float)ImGui::GetTime() * 0.35f, 1.2f);
    float sx = mn.x + (mx.x - mn.x) * (t * 1.4f - 0.2f);
    if (S.checker.Running())
        dl->AddRectFilledMultiColor(ImVec2(sx - 40, mn.y), ImVec2(sx + 10, mx.y),
                                    IM_COL32(255, 255, 255, 0),
                                    IM_COL32(255, 255, 255, 26),
                                    IM_COL32(255, 255, 255, 26),
                                    IM_COL32(255, 255, 255, 0));
    char txt[48];
    snprintf(txt, sizeof(txt), "%s %d%%", label, (int)(pct * 100));
    ImVec2 tsz = ImGui::CalcTextSize(txt);
    dl->AddText(ImVec2(mn.x + (mx.x - mn.x - tsz.x) * 0.5f, mn.y + (mx.y - mn.y - tsz.y) * 0.5f),
                ImGui::GetColorU32(theme::Pal().text), txt);
}

static void RenderCheckerPage(float w, float h) {
    auto& p = theme::Pal();
    ImGui::PushFont(g_fontBold);
    ImGui::TextUnformatted("Проверка аккаунтов");
    ImGui::PopFont();
    if (S.checker.Running()) {
        ImGui::SameLine(0, 12);
        if (S.checker.Paused()) {
            ImGui::TextDisabled("  пауза");
        } else {
            ImVec2 cp = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(20, 22));
            theme::Spinner(ImVec2(cp.x + 10, cp.y + 11), 8.5f, 2.2f,
                           ImGui::GetColorU32(theme::AccentGlow(0.95f)), 1.5f);
            ImGui::SameLine(0, 8);
            char prog[64];
            snprintf(prog, sizeof(prog), "%d/%d · %d/сек · ETA %02d:%02d",
                     S.checker.Done(), S.checker.Total(), S.checker.Cps(),
                     S.checker.EtaSec() / 60, S.checker.EtaSec() % 60);
            ImGui::TextDisabled("%s", prog);
        }
    } else {
        ImGui::SameLine(0, 12);
        ImGui::TextDisabled("  Space — старт/пауза");
    }

    float total = (float)std::max(S.checker.Total(), 1);
    float cardW = (w - 36.f) / 4.f;
    StatCard("##v", "ВАЛИДНЫЕ", S.dispValid, S.checker.Hits(),
             ImGui::GetColorU32(p.valid), S.checker.Hits() / total, cardW);
    ImGui::SameLine(0, 12);
    StatCard("##g", "STEAMGUARD", S.dispGuard, S.checker.Guards(),
             ImGui::GetColorU32(p.guard), S.checker.Guards() / total, cardW);
    ImGui::SameLine(0, 12);
    StatCard("##b", "НЕВАЛИДНЫЕ", S.dispBad, S.checker.Bads(),
             ImGui::GetColorU32(p.bad), S.checker.Bads() / total, cardW);
    ImGui::SameLine(0, 12);
    StatCard("##e", "ОШИБКИ", S.dispErr, S.checker.Errors(),
             ImGui::GetColorU32(p.warn), S.checker.Errors() / total, cardW);

    ImGui::Spacing();

    float barH = 26;
    ImVec2 barOrg = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, barH));
    ProgressPulse("Прогресс", barOrg, ImVec2(barOrg.x + w, barOrg.y + barH));

    ImGui::Spacing();
    ImGui::Spacing();

    float leftW = 372.0f;
    float bodyH = ImGui::GetContentRegionAvail().y;

    ImGui::BeginChild("left", ImVec2(leftW, bodyH));
    {
        ImVec2 org = ImGui::GetCursorScreenPos();
        float lw = leftW - 4;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float lh = 300;
        dl->AddRectFilled(org, ImVec2(org.x + lw, org.y + lh),
                          ImGui::GetColorU32(ImVec4(p.panelSoft.x, p.panelSoft.y,
                                                    p.panelSoft.z, 0.42f)), 14);
        dl->AddRect(org, ImVec2(org.x + lw, org.y + lh),
                    ImGui::GetColorU32(ImVec4(p.border.x, p.border.y, p.border.z, p.border.w)),
                    14, 0, 1.1f);

        ImGui::SetCursorPos(ImVec2(16, 14));
        ImGui::PushFont(g_fontBold);
        ImGui::TextUnformatted("Аккаунты для проверки");
        ImGui::PopFont();

        int lines = (int)ParseCombos(S.comboText).size();
        char chip[32];
        snprintf(chip, sizeof(chip), "%d", lines);
        ImVec2 tsz = ImGui::CalcTextSize(chip);
        ImVec2 chipPos(ImVec2(org.x + lw - tsz.x - 34, org.y + 12));
        dl->AddRectFilled(chipPos, ImVec2(chipPos.x + tsz.x + 20, chipPos.y + 22),
                          ImGui::GetColorU32(theme::AccentGlow(0.16f)), 11);
        dl->AddText(ImVec2(chipPos.x + 10, chipPos.y + 3),
                    ImGui::GetColorU32(theme::AccentGlow(0.95f)), chip);

        ImGui::SetCursorPos(ImVec2(16, 46));
        ImGui::PushFont(g_fontMono);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImVec2 ipos = ImGui::GetCursorScreenPos();
        ImGui::InputTextMultiline("##combo", &S.comboText, ImVec2(lw - 32, lh - 66));
        ImGui::PopStyleColor();
        ImGui::PopFont();
        if (S.comboText.empty()) {
            dl->AddText(g_fontMono, 16.f, ImVec2(ipos.x + 8, ipos.y + 8),
                        ImGui::GetColorU32(ImVec4(p.dim.x, p.dim.y, p.dim.z, 0.45f)),
                        "login:password\n\nexample:\nsomeuser123:MyStr0ngPass!\nanother_acc@hotmail.com:qwerty456\nuser:pass:mail:mailpass");
        }

        ImGui::SetCursorPos(ImVec2(16, lh + 12));
        bool running = S.checker.Running();
        bool paused = S.checker.Paused();
        int nb = running ? 3 : 2;
        float gapB = 10;
        float bwN = (lw - 32 - gapB * (nb - 1)) / nb;
        if (theme::GlowButton("##load", running ? "Загрузить" : "Загрузить .txt",
                              ImVec2(bwN, 40), false))
            LoadComboFile();
        Tooltip("Выбрать .txt файл с аккаунтами (login:password)");
        ImGui::SameLine(0, gapB);
        if (running) {
            if (paused) {
                if (theme::GlowButton("##resume", "Продолжить", ImVec2(bwN, 40), true))
                    TogglePause();
                Tooltip("Продолжить проверку (Space)");
            } else {
                if (theme::GlowButton("##pause", "Пауза", ImVec2(bwN, 40), false))
                    TogglePause();
                Tooltip("Поставить на паузу (Space)");
            }
            ImGui::SameLine(0, gapB);
            if (theme::GlowButton("##stop", "Стоп", ImVec2(bwN, 40), false))
                StopChecker();
            Tooltip("Остановить проверку");
        } else {
            if (theme::GlowButton("##start", "Запустить проверку", ImVec2(bwN, 40), true))
                StartChecker();
            Tooltip("Начать проверку загруженных аккаунтов (Space)");
        }

        ImGui::Spacing();
        ImGui::Spacing();

        char thrLabel[32];
        snprintf(thrLabel, sizeof(thrLabel), "Потоки: %d", S.threads);
        ImGui::PushFont(g_fontMain);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 6);
        ImGui::SliderInt("##thr", &S.threads, 1, 64, thrLabel);
        Tooltip("Одновременных проверок (больше — быстрее, но выше шанс rate limit)");
        ImGui::PopStyleVar();
        ImGui::PopFont();
    }
    ImGui::EndChild();

    ImGui::SameLine(0, 12);

    ImGui::BeginChild("right", ImVec2(w - leftW - 16, bodyH));
    {
        ImVec2 org = ImGui::GetCursorScreenPos();
        float rw = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float rh = ImGui::GetContentRegionAvail().y;
        dl->AddRectFilled(org, ImVec2(org.x + rw, org.y + rh),
                          ImGui::GetColorU32(ImVec4(p.panelSoft.x, p.panelSoft.y,
                                                    p.panelSoft.z, 0.42f)), 14);
        dl->AddRect(org, ImVec2(org.x + rw, org.y + rh),
                    ImGui::GetColorU32(ImVec4(p.border.x, p.border.y, p.border.z, p.border.w)),
                    14, 0, 1.1f);

        ImGui::SetCursorPos(ImVec2(16, 14));
        ImGui::PushFont(g_fontBold);
        ImGui::TextUnformatted("Лог проверки");
        ImGui::PopFont();

        const char* filters[5] = {"Все", "VALID", "2FA", "BAD", "ERR"};
        float fx = rw - 90 - 5 * 54 - 8;
        for (int i = 0; i < 5; i++) {
            ImGui::SetCursorPos(ImVec2(fx + i * 54, 12));
            bool sel = S.feedFilter == i;
            ImVec2 bpos = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton((std::string("flt") + std::to_string(i)).c_str(),
                                       ImVec2(50, 22))) {
                S.feedFilter = i;
            }
            ImDrawList* fdl = ImGui::GetWindowDrawList();
            ImU32 bg = sel ? ImGui::GetColorU32(theme::AccentGlow(0.25f))
                           : ImGui::GetColorU32(ImVec4(p.panelSoft.x, p.panelSoft.y,
                                                      p.panelSoft.z, 0.5f));
            fdl->AddRectFilled(bpos, ImVec2(bpos.x + 50, bpos.y + 22), bg, 7);
            if (sel)
                fdl->AddRect(bpos, ImVec2(bpos.x + 50, bpos.y + 22),
                             ImGui::GetColorU32(theme::AccentGlow(0.7f)), 7, 0, 1.1f);
            ImVec2 ftz = ImGui::CalcTextSize(filters[i]);
            ImU32 ftc = sel ? ImGui::GetColorU32(p.text) : ImGui::GetColorU32(p.dim);
            fdl->AddText(ImVec2(bpos.x + (50 - ftz.x) * 0.5f, bpos.y + (22 - ftz.y) * 0.5f),
                         ftc, filters[i]);
        }

        ImGui::SetCursorPos(ImVec2(rw - 84, 9));
        ImGui::PushID("clr");
        if (theme::IconButton("##c", g_icTrash.c_str(), 26)) {
            std::lock_guard<std::mutex> l(S.mtx);
            S.feed.clear();
        }
        Tooltip("Очистить лог");
        ImGui::PopID();

        ImGui::SetCursorPos(ImVec2(10, 50));
        ImGui::BeginChild("feedscroll", ImVec2(rw - 24, rh - 70), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        {
            std::vector<FeedItem> items;
            {
                std::lock_guard<std::mutex> l(S.mtx);
                items = S.feed;
            }
            long long now = util::NowMs();
            float y = 0;
            for (int i = (int)items.size() - 1; i >= 0; i--) {
                FeedItem& it = items[i];
                if (S.feedFilter == 1 && it.st != AccStatus::Valid) continue;
                if (S.feedFilter == 2 && it.st != AccStatus::Guard) continue;
                if (S.feedFilter == 3 && it.st != AccStatus::Invalid) continue;
                if (S.feedFilter == 4 && it.st != AccStatus::Error && it.st != AccStatus::RateLimited) continue;
                float age = (now - it.born) / 1000.f;
                float a = theme::Ease(age / 0.3f);
                float slide = (1.f - a) * 22.f;
                float rowH = 30;
                ImVec2 pos(12 + slide, y);
                ImGui::SetCursorPos(pos);
                ImGui::SetNextItemAllowOverlap();
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);

                ImDrawList* rdl = ImGui::GetWindowDrawList();
                ImVec2 sp = ImGui::GetCursorScreenPos();

                char timebuf[16];
                snprintf(timebuf, sizeof(timebuf), "%s",
                         util::FormatClock(it.born).c_str());
                rdl->AddText(ImVec2(sp.x + 4, sp.y + 3),
                             ImGui::GetColorU32(ImVec4(p.dim.x, p.dim.y, p.dim.z, 0.7f)),
                             timebuf);
                rdl->AddText(ImVec2(sp.x + 84, sp.y + 3),
                             ImGui::GetColorU32(StatusColor(it.st)),
                             StatusLabel(it.st));
                rdl->AddText(ImVec2(sp.x + 140, sp.y + 3),
                             ImGui::GetColorU32(ImVec4(p.text.x, p.text.y, p.text.z, a)),
                             it.user.c_str());
                if (!it.msg.empty() && it.msg != "network")
                    rdl->AddText(ImVec2(sp.x + 140 + ImGui::CalcTextSize(it.user.c_str()).x + 12, sp.y + 3),
                                 ImGui::GetColorU32(ImVec4(p.dim.x, p.dim.y, p.dim.z, 0.55f * a)),
                                 it.msg.c_str());
                y += rowH;
                ImGui::PopStyleVar();
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

static void RenderAccountsPage(float w, float h) {
    auto& p = theme::Pal();
    float startY = ImGui::GetCursorPosY();
    ImGui::PushFont(g_fontBold);
    char title[80];
    snprintf(title, sizeof(title), "Каталог аккаунтов  ·  %d", S.store.CountValid() + S.store.CountGuard());
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::SameLine();

    ImGui::SetCursorPosX(w - 330);
    ImGui::SetNextItemWidth(200);
    ImGui::PushStyleColor(ImGuiCol_Text, p.text);
    ImGui::InputTextWithHint("##srch", "Поиск...", S.search, sizeof(S.search));
    Tooltip("Фильтр по логину");
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 8);
    if (theme::IconButton("##exp", g_icSave.c_str(), 30)) {
        ExportHits(S.dataDir + "\\hits.txt", S.store.Items());
        PushToast(1, "Экспортировано в hits.txt");
    }
    Tooltip("Экспорт валидных в hits.txt");
    ImGui::SameLine(0, 4);
    if (theme::IconButton("##ref", g_icRefresh.c_str(), 30)) RefreshSteamInfo();
    Tooltip("Обновить статус Steam");
    ImGui::SameLine(0, 4);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, S.sortByNew ? 1.f : 0.45f);
    if (theme::IconButton("##sort", g_icRefresh.c_str(), 30,
                          ImGui::GetColorU32(S.sortByNew ? theme::AccentGlow(0.95f)
                                                         : p.dim)))
        S.sortByNew = !S.sortByNew;
    ImGui::PopStyleVar();
    Tooltip(S.sortByNew ? "Сортировка: новые сверху (клик — по алфавиту)"
                        : "Сортировка: по алфавиту (клик — новые сверху)");

    ImGui::Spacing();

    {
        ImVec2 org = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float bh = 54;
        dl->AddRectFilled(org, ImVec2(org.x + w, org.y + bh),
                          ImGui::GetColorU32(ImVec4(p.panelSoft.x, p.panelSoft.y,
                                                    p.panelSoft.z, 0.5f)), 12);
        const char* stTxt = S.steamRunning ? "ONLINE" : "OFFLINE";
        ImVec4 stCol = S.steamRunning ? p.valid : p.bad;
        dl->AddText(ImVec2(org.x + 18, org.y + bh * 0.5f - 8),
                    ImGui::GetColorU32(stCol), stTxt);
        ImVec2 stw = ImGui::CalcTextSize(stTxt);
        char sess[160];
        snprintf(sess, sizeof(sess), "  ·  текущий пользователь: %s",
                 S.currentUser.empty() ? "-" : S.currentUser.c_str());
        dl->AddText(ImVec2(org.x + 18 + stw.x + 6, org.y + bh * 0.5f - 8),
                    ImGui::GetColorU32(ImVec4(p.dim.x, p.dim.y, p.dim.z, 0.85f)),
                    sess);
        ImGui::Dummy(ImVec2(w, bh + 8));
    }

    float listH = h - (ImGui::GetCursorPosY() - startY);
    ImGui::BeginChild("accscroll", ImVec2(w, listH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
    {
        std::string q = util::ToLower(S.search);
        int shown = 0;
        auto& items = S.store.Items();
        std::vector<Account*> rows;
        for (auto& a : items)
            if (a.status == AccStatus::Valid || a.status == AccStatus::Guard)
                if (q.empty() || util::ToLower(a.user).find(q) != std::string::npos)
                    rows.push_back(&a);
        if (S.sortByNew && rows.size() > 1)
            std::sort(rows.begin(), rows.end(),
                      [](const Account* a, const Account* b) { return a->addedAt > b->addedAt; });
        for (auto* pa : rows) {
            Account& a = *pa;
            shown++;

            float rowH = 52;
            ImVec2 org = ImGui::GetCursorScreenPos();
            float rw = ImGui::GetContentRegionAvail().x;
            ImDrawList* dl = ImGui::GetWindowDrawList();

            bool hov = ImGui::IsMouseHoveringRect(org, ImVec2(org.x + rw, org.y + rowH));
            dl->AddRectFilled(org, ImVec2(org.x + rw, org.y + rowH),
                              hov ? AccentHoverRow() : RowBg(), 11);

            ImVec2 av(org.x + 12, org.y + 9);
            float ar = 17;
            dl->AddCircleFilled(ImVec2(av.x + ar, av.y + ar), ar,
                                ImGui::GetColorU32(theme::AccentGlow(0.18f)), 32);
            char initial[2] = {(char)toupper(a.user[0]), 0};
            ImVec2 isz = ImGui::CalcTextSize(initial);
            dl->AddText(ImVec2(av.x + ar - isz.x * 0.5f, av.y + ar - isz.y * 0.5f),
                        ImGui::GetColorU32(theme::AccentGlow(0.95f)), initial);

            ImGui::PushFont(g_fontBold);
            dl->AddText(ImVec2(org.x + 58, org.y + 9),
                        ImGui::GetColorU32(p.text), a.user.c_str());
            ImGui::PopFont();

            std::string masked;
            if (S.maskPass) {
                size_t n = std::min(a.pass.size(), (size_t)12);
                for (size_t k = 0; k < n; k++) masked += "\xE2\x80\xA2";
            } else {
                masked = a.pass;
            }
            dl->AddText(ImVec2(org.x + 58, org.y + 29),
                        ImGui::GetColorU32(ImVec4(p.dim.x, p.dim.y, p.dim.z, 0.9f)),
                        masked.c_str());

            if (a.status == AccStatus::Guard) {
                ImGui::SetCursorScreenPos(ImVec2(org.x + rw - 320, org.y + 16));
                theme::Badge("2FA", ImGui::GetColorU32(p.guard));
            } else {
                ImGui::SetCursorScreenPos(ImVec2(org.x + rw - 320, org.y + 16));
                theme::Badge("VALID", ImGui::GetColorU32(p.valid));
            }

            bool busy = S.loggingUser == a.user;
            float bx = org.x + rw - 240;
            ImGui::SetCursorScreenPos(ImVec2(bx, org.y + 10));
            ImGui::BeginGroup();
            if (busy) {
                theme::Spinner(ImVec2(bx + 40, org.y + rowH * 0.5f), 10, 2.4f,
                               ImGui::GetColorU32(theme::AccentGlow(0.95f)), 1.6f);
                ImGui::Dummy(ImVec2(80, 30));
            } else if (theme::GlowButton(("in" + a.user).c_str(), "Войти",
                                         ImVec2(84, 32), true)) {
                DoLoginAsync(a.user, a.pass);
            }
            Tooltip("Выйти из текущего Steam и войти в этот аккаунт");
            ImGui::EndGroup();

            ImGui::SetCursorScreenPos(ImVec2(bx + 96, org.y + 12));
            if (theme::IconButton(("cu" + a.user).c_str(), g_icCopy.c_str(), 28)) {
                OpenClipboard(g_hwnd);
                EmptyClipboard();
                HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, a.user.size() + 1);
                memcpy(GlobalLock(hg), a.user.c_str(), a.user.size() + 1);
                GlobalUnlock(hg);
                SetClipboardData(CF_TEXT, hg);
                CloseClipboard();
                PushToast(0, "Логин скопирован");
            }
            Tooltip("Копировать логин");
            ImGui::SameLine(0, 2);
            if (theme::IconButton(("cp" + a.user).c_str(), g_icCopy.c_str(), 28,
                                  ImGui::GetColorU32(p.dim))) {
                OpenClipboard(g_hwnd);
                EmptyClipboard();
                HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, a.pass.size() + 1);
                memcpy(GlobalLock(hg), a.pass.c_str(), a.pass.size() + 1);
                GlobalUnlock(hg);
                SetClipboardData(CF_TEXT, hg);
                CloseClipboard();
                PushToast(0, "Пароль скопирован");
            }
            Tooltip("Копировать пароль");
            ImGui::SameLine(0, 2);
            if (theme::IconButton(("del" + a.user).c_str(), g_icTrash.c_str(), 28,
                                  ImGui::GetColorU32(ImVec4(p.bad.x, p.bad.y, p.bad.z, 0.8f)))) {
                S.store.Remove(a.user);
                S.store.Save(S.dataDir + "\\accounts.txt");
                PushToast(2, "Аккаунт удалён из каталога");
            }
            Tooltip("Удалить из каталога");

            ImGui::SetCursorScreenPos(org);
            ImGui::Dummy(ImVec2(rw, rowH));
        }

        if (shown == 0) {
            ImVec2 org = ImGui::GetCursorScreenPos();
            org.x += ImGui::GetContentRegionAvail().x * 0.5f - 90;
            org.y += 60;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(g_fontBold, 19, org, ImGui::GetColorU32(ImVec4(p.dim.x, p.dim.y, p.dim.z, 0.7f)),
                        "Здесь появятся валидные аккаунты");
            dl->AddText(ImVec2(org.x + 22, org.y + 30),
                        ImGui::GetColorU32(ImVec4(p.dim.x, p.dim.y, p.dim.z, 0.45f)),
                        "после успешной проверки");
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

static ImU32 RowBg() {
    auto& p = theme::Pal();
    return ImGui::GetColorU32(ImVec4(p.panelSoft.x, p.panelSoft.y, p.panelSoft.z, 0.42f));
}

static ImU32 AccentHoverRow() {
    auto& p = theme::Pal();
    return ImGui::GetColorU32(theme::AccentGlow(0.08f));
}

static void ToggleRow(const char* label, const char* id, bool* v) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 52);
    theme::Toggle(id, v);
}

static void RenderSettingsPage(float w, float h) {
    auto& p = theme::Pal();
    ImGui::PushFont(g_fontBold);
    ImGui::TextUnformatted("Настройки");
    ImGui::PopFont();
    ImGui::Spacing();

    float colW = (w - 12) * 0.5f;
    float bodyH = ImGui::GetContentRegionAvail().y;

    ImGui::BeginChild("setl", ImVec2(colW, bodyH));
    {
        ImVec2 org = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float ph = bodyH;
        dl->AddRectFilled(org, ImVec2(org.x + colW - 4, org.y + ph),
                          ImGui::GetColorU32(ImVec4(p.panelSoft.x, p.panelSoft.y,
                                                    p.panelSoft.z, 0.42f)), 14);
        dl->AddRect(org, ImVec2(org.x + colW - 4, org.y + ph),
                    ImGui::GetColorU32(ImVec4(p.border.x, p.border.y, p.border.z, p.border.w)),
                    14, 0, 1.1f);

        ImGui::SetCursorPos(ImVec2(18, 16));
        ImGui::PushFont(g_fontBold);
        ImGui::TextUnformatted("Внешний вид");
        ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(18, 50));
        const char* names[6] = {"Аметист", "Мята", "Закат", "Океан", "Янтарь", "Орхидея"};
        for (int i = 0; i < 6; i++) {
            ImGui::SetCursorPos(ImVec2(18.f + i * 52.f, 52.f));
            ImGui::PushID(i);
            ImVec2 sw(ImGui::GetCursorScreenPos());
            bool sel = theme::AccentPreset() == i;
            if (ImGui::InvisibleButton("sw", ImVec2(40, 40)))
                theme::SetAccentPreset(i);
            ImU32 c1 = ImGui::GetColorU32(kPresetsA()[i]);
            ImU32 c2 = ImGui::GetColorU32(kPresetsB()[i]);
            dl->AddCircleFilled(ImVec2(sw.x + 20, sw.y + 20), 14, c1, 24);
            dl->AddTriangleFilled(ImVec2(sw.x + 20, sw.y + 20),
                                  ImVec2(sw.x + 34, sw.y + 20),
                                  ImVec2(sw.x + 27, sw.y + 7), c2);
            if (sel) {
                dl->AddCircle(ImVec2(sw.x + 20, sw.y + 20), 18,
                              ImGui::GetColorU32(p.text), 24, 1.6f);
            }
            ImGui::PopID();
        }
        ImGui::SetCursorPos(ImVec2(18, 104));
        ImGui::TextDisabled("%s", names[theme::AccentPreset()]);

        ImGui::SetCursorPos(ImVec2(18, 138));
        bool pS = S.soundHit, pM = S.maskPass, pA = S.autoExport, pK = S.skipKnown;
        ImGui::BeginGroup();
        ToggleRow("Звук при находке", "##snd", &S.soundHit);
        ImGui::Spacing();
        ToggleRow("Скрывать пароли", "##msk", &S.maskPass);
        ImGui::Spacing();
        ToggleRow("Автоэкспорт hits.txt", "##aex", &S.autoExport);
        ImGui::Spacing();
        ToggleRow("Пропускать известных", "##skw", &S.skipKnown);
        Tooltip("Не проверять аккаунты, уже лежащие в каталоге");
        ImGui::EndGroup();
        if (pS != S.soundHit || pM != S.maskPass || pA != S.autoExport || pK != S.skipKnown)
            SaveSettings();

        ImGui::SetCursorPos(ImVec2(18, 296));
        if (theme::GlowButton("##rst", "Сбросить настройки", ImVec2(colW - 40, 36), false)) {
            S.threads = 8;
            S.soundHit = true;
            S.maskPass = true;
            S.autoExport = true;
            S.skipKnown = true;
            theme::SetAccentPreset(0);
            SaveSettings();
            PushToast(0, "Настройки сброшены");
        }
        Tooltip("Вернуть всё к дефолту");
    }
    ImGui::EndChild();

    ImGui::SameLine(0, 12);

    ImGui::BeginChild("setr", ImVec2(w - colW - 16, bodyH));
    {
        ImVec2 org = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float pw = ImGui::GetContentRegionAvail().x;
        float ph = bodyH;
        dl->AddRectFilled(org, ImVec2(org.x + pw, org.y + ph),
                          ImGui::GetColorU32(ImVec4(p.panelSoft.x, p.panelSoft.y,
                                                    p.panelSoft.z, 0.42f)), 14);
        dl->AddRect(org, ImVec2(org.x + pw, org.y + ph),
                    ImGui::GetColorU32(ImVec4(p.border.x, p.border.y, p.border.z, p.border.w)),
                    14, 0, 1.1f);

        ImGui::SetCursorPos(ImVec2(18, 16));
        ImGui::PushFont(g_fontBold);
        ImGui::TextUnformatted("Прокси и пути");
        ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(18, 50));
        char plabel[48];
        snprintf(plabel, sizeof(plabel), "Загружено прокси: %d", (int)S.proxies.size());
        ImGui::TextDisabled("%s", plabel);
        ImGui::SetCursorPos(ImVec2(18, 74));
        if (theme::GlowButton("##px", "Загрузить proxy.txt", ImVec2(pw - 40, 38), false))
            LoadProxyFile();
        Tooltip("Формат: host:port или user:pass@host:port, по одному в строке");

        ImGui::SetCursorPos(ImVec2(18, 130));
        ImGui::TextDisabled("Каталог данных:");
        ImGui::PushFont(g_fontMono);
        ImGui::SetCursorPos(ImVec2(18, 152));
        ImGui::TextUnformatted(S.dataDir.c_str());
        ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(18, 182));
        ImGui::TextDisabled("Steam:");
        ImGui::PushFont(g_fontMono);
        ImGui::SetCursorPos(ImVec2(18, 204));
        ImGui::TextUnformatted(S.steamPath.empty() ? "не найден" : S.steamPath.c_str());
        ImGui::PopFont();

        float aboutY = ph - 118;
        if (aboutY > 240) {
            dl->AddRectFilled(ImVec2(org.x + 14, org.y + aboutY),
                              ImVec2(org.x + pw - 14, org.y + ph - 14),
                              ImGui::GetColorU32(theme::AccentGlow(0.07f)), 12);
            if (g_logo)
                dl->AddImageRounded(g_logo, ImVec2(org.x + 26, org.y + aboutY + 14),
                                    ImVec2(org.x + 62, org.y + aboutY + 50),
                                    ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8);
            dl->AddText(ImVec2(org.x + 76, org.y + aboutY + 16),
                        ImGui::GetColorU32(p.text), "AvirA Steam Tool");
            dl->AddText(ImVec2(org.x + 76, org.y + aboutY + 36),
                        ImGui::GetColorU32(ImVec4(p.dim.x, p.dim.y, p.dim.z, 0.8f)),
                        "v1.1 · C++ / DX11 / WinHTTP");
            dl->AddText(ImVec2(org.x + 76, org.y + aboutY + 54),
                        ImGui::GetColorU32(ImVec4(p.dim.x, p.dim.y, p.dim.z, 0.55f)),
                        "github.com/ANTONSVD/AvirA-Steam-Tool");
        }
    }
    ImGui::EndChild();
}

static const ImVec4* kPresetsA() {
    static ImVec4 arr[6] = {
        ImVec4(0.545f, 0.361f, 1.000f, 1.f), ImVec4(0.302f, 0.863f, 0.698f, 1.f),
        ImVec4(1.000f, 0.427f, 0.588f, 1.f), ImVec4(0.416f, 0.639f, 1.000f, 1.f),
        ImVec4(0.973f, 0.647f, 0.302f, 1.f), ImVec4(0.702f, 0.455f, 1.000f, 1.f)};
    return arr;
}
static const ImVec4* kPresetsB() {
    static ImVec4 arr[6] = {
        ImVec4(0.741f, 0.565f, 1.000f, 1.f), ImVec4(0.796f, 0.937f, 0.404f, 1.f),
        ImVec4(1.000f, 0.678f, 0.400f, 1.f), ImVec4(0.573f, 0.447f, 1.000f, 1.f),
        ImVec4(1.000f, 0.867f, 0.361f, 1.f), ImVec4(1.000f, 0.494f, 0.859f, 1.f)};
    return arr;
}

static void DrawToasts() {
    std::vector<ToastItem> items;
    {
        std::lock_guard<std::mutex> l(S.mtx);
        items.assign(S.toasts.begin(), S.toasts.end());
        long long nowCut = util::NowMs();
        for (size_t i = 0; i < S.toasts.size();) {
            if ((nowCut - S.toasts[i].born) >= 4200)
                S.toasts.erase(S.toasts.begin() + i);
            else
                i++;
        }
    }
    if (items.empty()) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    long long now = util::NowMs();
    float life = 3.4f;
    float tw = 330, th = 44;
    float xRight = S.winPos.x + S.winSize.x - 24;
    float yBase = S.winPos.y + S.winSize.y - 24;

    int slot = 0;
    for (int i = (int)items.size() - 1; i >= 0; i--) {
        const ToastItem& t = items[i];
        float age = (now - t.born) / 1000.f;
        if (age < 0.f || age > life) continue;
        float ain = EaseBack(age / 0.35f);
        float fout = age > life - 0.3f ? (life - age) / 0.3f : 1.f;
        float alpha = ain * fout;
        if (alpha <= 0.01f) continue;

        ImVec4 col;
        switch (t.type) {
            case 1: col = theme::Pal().valid; break;
            case 2: col = theme::Pal().guard; break;
            case 3: col = theme::Pal().bad; break;
            default: col = theme::Pal().accent; break;
        }

        float slide = (1.f - ain) * 60.f;
        float yOff = slot * (th + 8);
        slot++;
        ImVec2 mn(xRight - tw + slide, yBase - yOff - th);
        ImVec2 mx(xRight + slide, yBase - yOff);

        dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImVec4(
                                      0.078f, 0.082f, 0.118f, 0.98f * alpha)), 10);
        dl->AddRect(mn, mx, ImGui::GetColorU32(
                                ImVec4(1, 1, 1, 0.07f * alpha)), 10, 0, 1.f);
        dl->AddRectFilled(mn, ImVec2(mn.x + 3, mx.y),
                          ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, alpha)),
                          10, ImDrawFlags_RoundCornersLeft);

        ImVec2 tsz = ImGui::CalcTextSize(t.text.c_str());
        dl->PushClipRect(ImVec2(mn.x + 16, mn.y), ImVec2(mx.x - 10, mx.y), true);
        dl->AddText(ImVec2(mn.x + 16, (mn.y + mx.y - tsz.y) * 0.5f),
                    ImGui::GetColorU32(ImVec4(theme::Pal().text.x, theme::Pal().text.y,
                                              theme::Pal().text.z, 0.95f * alpha)),
                    t.text.c_str());
        dl->PopClipRect();

        float pw = (mx.x - mn.x - 6) * (1.f - age / life);
        dl->AddRectFilled(ImVec2(mn.x + 3, mx.y - 2), ImVec2(mn.x + 3 + pw, mx.y),
                          ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.55f * alpha)),
                          10, ImDrawFlags_RoundCornersBottom);
    }
}

static void DrawTip() {
    if (!g_tip.active || g_tip.text.empty()) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 tsz = ImGui::CalcTextSize(g_tip.text.c_str());
    float tw = tsz.x + 26;
    float thh = tsz.y + 16;

    float x = g_tip.mn.x;
    float y = g_tip.mx.y + 8;
    if (x + tw > S.winPos.x + S.winSize.x - 12) x = S.winPos.x + S.winSize.x - 12 - tw;
    if (x < S.winPos.x + 12) x = S.winPos.x + 12;
    if (y + thh > S.winPos.y + S.winSize.y - 12) y = g_tip.mn.y - thh - 8;

    ImVec4 col = theme::Pal().accent;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + tw, y + thh),
                      ImGui::GetColorU32(ImVec4(0.078f, 0.082f, 0.118f, 0.99f)), 8);
    dl->AddRect(ImVec2(x, y), ImVec2(x + tw, y + thh),
                ImGui::GetColorU32(ImVec4(1, 1, 1, 0.09f)), 8, 0, 1.f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 3, y + thh),
                      ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.9f)), 8,
                      ImDrawFlags_RoundCornersLeft);
    dl->AddText(ImVec2(x + 14, y + (thh - tsz.y) * 0.5f),
                ImGui::GetColorU32(ImVec4(theme::Pal().text.x, theme::Pal().text.y,
                                          theme::Pal().text.z, 0.92f)),
                g_tip.text.c_str());
}

void Render() {
    ImGuiIO& io = ImGui::GetIO();
    float dt = io.DeltaTime;
    theme::Tick(dt);
    DrainEvents();
    g_tip = TipState{};

    S.intro = theme::Approach(S.intro, 1.0f, 3.2f, dt);
    S.pageAnim = theme::Approach(S.pageAnim, 1.0f, 9.f, dt);

    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space)) {
        if (S.checker.Running())
            TogglePause();
        else
            StartChecker();
    }

    float targetV = (float)S.checker.Hits();
    S.dispValid = theme::Approach(S.dispValid, targetV, 8.f, dt);
    S.dispGuard = theme::Approach(S.dispGuard, (float)S.checker.Guards(), 8.f, dt);
    S.dispBad = theme::Approach(S.dispBad, (float)S.checker.Bads(), 8.f, dt);
    S.dispErr = theme::Approach(S.dispErr, (float)S.checker.Errors(), 8.f, dt);

    if (util::NowMs() - S.lastSteamCheck > 2500) RefreshSteamInfo();

    ImGuiStyle& st = ImGui::GetStyle();
    float savedAlpha = st.Alpha;
    st.Alpha = theme::Ease(S.intro);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::Pal().bg);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);

    S.winPos = ImGui::GetWindowPos();
    S.winSize = ImGui::GetWindowSize();
    float w = S.winSize.x;
    DrawBackground();

    ImGui::SetCursorScreenPos(S.winPos);
    ImGui::BeginChild("content", S.winSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

    ImGui::SetCursorScreenPos(ImVec2(S.winPos.x, S.winPos.y + (1 - theme::Ease(S.intro)) * 26));
    ImGui::BeginGroup();
    DrawTitlebar(w);

    float pad = 16;
    float sideW = 208;
    ImVec2 bodyOrg(ImGui::GetCursorScreenPos().x + pad, ImGui::GetCursorScreenPos().y + 4);
    float bodyH = S.winSize.y - (bodyOrg.y - S.winPos.y) - pad - 12;

    ImGui::SetCursorScreenPos(bodyOrg);
    ImGui::BeginChild("sidebar", ImVec2(sideW, bodyH));
    {
        auto& p = theme::Pal();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 o = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(o, ImVec2(o.x + sideW, o.y + bodyH),
                          ImGui::GetColorU32(ImVec4(p.panelSoft.x, p.panelSoft.y,
                                                    p.panelSoft.z, 0.42f)), 16);
        dl->AddRect(o, ImVec2(o.x + sideW, o.y + bodyH),
                    ImGui::GetColorU32(ImVec4(p.border.x, p.border.y, p.border.z, p.border.w)),
                    16, 0, 1.1f);

        ImGui::SetCursorPos(ImVec2(0, 10));
        const char* labels[3] = {"Проверка", "Аккаунты", "Настройки"};
        for (int i = 0; i < 3; i++) {
            if (NavItem(i, labels[i], sideW)) {
                if (S.page != i) {
                    S.pageDir = i > S.page ? 1.f : -1.f;
                    S.page = i;
                    S.pageAnim = 0.f;
                }
            }
            ImGui::Spacing();
        }

        float cardY = bodyH - 118;
        ImGui::SetCursorPos(ImVec2(14, cardY));
        ImVec2 co = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(co, ImVec2(co.x + sideW - 28, co.y + 96),
                          ImGui::GetColorU32(theme::AccentGlow(0.08f)), 13);
        dl->AddText(ImVec2(co.x + 16, co.y + 16), ImGui::GetColorU32(p.text), "AvirA Checker");
        char ver[64];
        snprintf(ver, sizeof(ver), "v1.0 · valid %d · 2fa %d",
                 S.store.CountValid(), S.store.CountGuard());
        dl->AddText(ImVec2(co.x + 16, co.y + 34),
                    ImGui::GetColorU32(ImVec4(p.dim.x, p.dim.y, p.dim.z, 0.85f)), ver);
        dl->AddText(ImVec2(co.x + 16, co.y + 62),
                    ImGui::GetColorU32(ImVec4(p.dim.x, p.dim.y, p.dim.z, 0.55f)),
                    S.checker.Running() ? "сканирование..." : "ожидание");
    }
    ImGui::EndChild();

    float contentX = bodyOrg.x + sideW + 14;
    float contentW = w - contentX - pad;

    float pa = theme::Ease(S.pageAnim);
    float off = (1.f - pa) * 26.f * S.pageDir;
    ImGui::SetCursorScreenPos(ImVec2(contentX + off, bodyOrg.y));
    ImGui::PushClipRect(ImVec2(contentX, bodyOrg.y),
                        ImVec2(contentX + contentW, bodyOrg.y + bodyH), true);
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, savedAlpha * pa);
    switch (S.page) {
        case PageChecker: RenderCheckerPage(contentW, bodyH); break;
        case PageAccounts: RenderAccountsPage(contentW, bodyH); break;
        case PageSettings: RenderSettingsPage(contentW, bodyH); break;
    }
    ImGui::PopStyleVar();
    ImGui::EndGroup();
    ImGui::PopClipRect();

    ImGui::EndGroup();
    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    st.Alpha = savedAlpha;
    DrawToasts();
    DrawTip();
}

}
