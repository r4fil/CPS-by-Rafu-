// CPS by Rafu - modern CPS control panel.
// Developed by Rafu.
// Uses the public Win32 SendInput API and a documented low-level mouse hook.
// No drivers or stealth features.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include "resource.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// ---------- IDs ----------
enum { IDC_SLIDER = 101, IDC_START, IDC_STOP, IDC_HK_START, IDC_HK_STOP, IDC_HK_ESTOP,
       IDC_PRESET10, IDC_PRESET13, IDC_PRESET17 };
enum { HK_START = 1, HK_STOP, HK_ESTOP };
enum { TIMER_UI = 1 };

// ---------- Palette ----------
static const COLORREF C_BG      = RGB(12, 16, 24);
static const COLORREF C_PANEL   = RGB(23, 29, 42);
static const COLORREF C_PANEL2  = RGB(29, 37, 52);
static const COLORREF C_BORDER  = RGB(43, 55, 74);
static const COLORREF C_TEXT    = RGB(237, 242, 250);
static const COLORREF C_MUTED   = RGB(139, 151, 170);
static const COLORREF C_GREEN   = RGB(52, 211, 153);
static const COLORREF C_RED     = RGB(251, 113, 133);
static const COLORREF C_ACCENT  = RGB(34, 211, 238);
static const COLORREF C_BLUE    = RGB(96, 165, 250);

// ---------- State ----------
static std::atomic<bool>      g_running{false};
static std::atomic<bool>      g_quit{false};
static std::atomic<int>       g_cps{10};
static std::atomic<long long> g_clicks{0};
static std::atomic<bool>      g_mouseHeld{false};
static std::atomic<bool>      g_swallowPhysicalUp{false};
static HANDLE g_runEvt  = nullptr;   // signalled while running
static HANDLE g_stopEvt = nullptr;   // signalled while stopped (wakes waits instantly)
static HANDLE g_mouseEvt = nullptr;  // signalled while physical left button is held
static HANDLE g_mouseChangedEvt = nullptr;
static HANDLE g_thread  = nullptr;
static HHOOK  g_mouseHook = nullptr;
static HWND   g_main = nullptr, g_slider, g_btnStart, g_btnStop, g_btn10, g_btn13, g_btn17,
              g_hkStart, g_hkStop, g_hkEstop;
static HFONT  g_fTitle, g_fBig, g_fNorm, g_fSmall, g_fBold;
static HBRUSH g_brBg;
static int    g_dpi = 96;
static wchar_t g_msg[160] = L"";
static DWORD   g_msgUntil = 0;
static WORD    g_hkStartVal, g_hkStopVal, g_hkEstopVal; // hotkey-control format: LOBYTE=vk, HIBYTE=HOTKEYF_*
static wchar_t g_iniPath[MAX_PATH];

static int S(int v) { return MulDiv(v, g_dpi, 96); }

static void SetMsg(const wchar_t* m, DWORD ms = 4000) {
    wcsncpy(g_msg, m, 159); g_msg[159] = 0;
    g_msgUntil = GetTickCount() + ms;
    if (g_main) InvalidateRect(g_main, nullptr, FALSE);
}

// ---------- Settings (plain .ini in %APPDATA%\CPSBooster) ----------
static void IniPath() {
    wchar_t dir[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH)) {
        wcscat(dir, L"\\CPSByRafu");
        CreateDirectoryW(dir, nullptr);
        swprintf(g_iniPath, MAX_PATH, L"%s\\settings.ini", dir);
    } else g_iniPath[0] = 0;
}
static void SaveSettings() {
    if (!g_iniPath[0]) return;
    wchar_t b[32];
    swprintf(b, 32, L"%d", g_cps.load());        WritePrivateProfileStringW(L"CPS", L"cps", b, g_iniPath);
    swprintf(b, 32, L"%d", (int)g_hkStartVal);   WritePrivateProfileStringW(L"CPS", L"start", b, g_iniPath);
    swprintf(b, 32, L"%d", (int)g_hkStopVal);    WritePrivateProfileStringW(L"CPS", L"stop", b, g_iniPath);
    swprintf(b, 32, L"%d", (int)g_hkEstopVal);   WritePrivateProfileStringW(L"CPS", L"estop", b, g_iniPath);
}
static void LoadSettings() {
    g_hkStartVal  = MAKEWORD(VK_F6, 0);
    g_hkStopVal   = MAKEWORD(VK_F7, 0);
    g_hkEstopVal  = MAKEWORD(VK_F8, 0);
    if (!g_iniPath[0]) return;
    int c = GetPrivateProfileIntW(L"CPS", L"cps", 10, g_iniPath);
    g_cps = c < 1 ? 1 : (c > 60 ? 60 : c);
    int oldToggle = GetPrivateProfileIntW(L"CPS", L"toggle", (int)g_hkStartVal, g_iniPath);
    int s = GetPrivateProfileIntW(L"CPS", L"start", oldToggle, g_iniPath);
    int p = GetPrivateProfileIntW(L"CPS", L"stop", (int)g_hkStopVal, g_iniPath);
    int e = GetPrivateProfileIntW(L"CPS", L"estop",  (int)g_hkEstopVal,  g_iniPath);
    if (LOBYTE(s)) g_hkStartVal = (WORD)s;
    if (LOBYTE(p)) g_hkStopVal = (WORD)p;
    if (LOBYTE(e)) g_hkEstopVal = (WORD)e;
    if (g_hkStartVal == g_hkStopVal || g_hkStartVal == g_hkEstopVal || g_hkStopVal == g_hkEstopVal) {
        g_hkStartVal = MAKEWORD(VK_F6, 0);
        g_hkStopVal = MAKEWORD(VK_F7, 0);
        g_hkEstopVal = MAKEWORD(VK_F8, 0);
    }
}

// ---------- Hotkeys ----------
static UINT ToModFlags(WORD v) {
    BYTE m = HIBYTE(v); UINT f = MOD_NOREPEAT;
    if (m & HOTKEYF_SHIFT)   f |= MOD_SHIFT;
    if (m & HOTKEYF_CONTROL) f |= MOD_CONTROL;
    if (m & HOTKEYF_ALT)     f |= MOD_ALT;
    return f;
}
static bool RegisterOne(int id, WORD v) {
    UnregisterHotKey(g_main, id);
    return RegisterHotKey(g_main, id, ToModFlags(v), LOBYTE(v)) != 0;
}
static void SyncHotkeyControls() {
    SendMessageW(g_hkStart,  HKM_SETHOTKEY, g_hkStartVal, 0);
    SendMessageW(g_hkStop,   HKM_SETHOTKEY, g_hkStopVal, 0);
    SendMessageW(g_hkEstop,  HKM_SETHOTKEY, g_hkEstopVal,  0);
}

// ---------- Start / stop ----------
static void UpdateUI() {
    bool r = g_running;
    EnableWindow(g_btnStart, !r);
    EnableWindow(g_btnStop, r);
    InvalidateRect(g_btnStart, nullptr, TRUE);
    InvalidateRect(g_btnStop, nullptr, TRUE);
    InvalidateRect(g_btn10, nullptr, TRUE);
    InvalidateRect(g_btn13, nullptr, TRUE);
    InvalidateRect(g_btn17, nullptr, TRUE);
    wchar_t t[96];
    if (r) swprintf(t, 96, L"CPS by Rafu  |  ARMED at %d CPS", g_cps.load());
    else   wcscpy(t, L"CPS by Rafu  |  Developed by Rafu");
    SetWindowTextW(g_main, t);
    InvalidateRect(g_main, nullptr, FALSE);
}
static void SetRunning(bool r) {
    if (g_running == r) return;
    if (r) g_clicks = 0;
    g_running = r;
    if (r) { ResetEvent(g_stopEvt); SetEvent(g_runEvt); }
    else   {
        g_mouseHeld = false;
        ResetEvent(g_mouseEvt);
        ResetEvent(g_runEvt);
        SetEvent(g_stopEvt);
        SetEvent(g_mouseChangedEvt);
    }
    if (r) SetTimer(g_main, TIMER_UI, 100, nullptr); else KillTimer(g_main, TIMER_UI);
    UpdateUI();
}

// ---------- Click worker ----------
static bool CursorOverOwnWindow() {
    POINT p; if (!GetCursorPos(&p)) return false;
    HWND h = WindowFromPoint(p);
    return h && GetAncestor(h, GA_ROOT) == g_main;   // never click our own controls
}
static LRESULT CALLBACK MouseHookProc(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        const MSLLHOOKSTRUCT* m = (const MSLLHOOKSTRUCT*)data;
        if (!(m->flags & LLMHF_INJECTED)) {
            if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) {
                if (g_running && !CursorOverOwnWindow()) {
                    g_mouseHeld = true;
                    g_swallowPhysicalUp = true;
                    SetEvent(g_mouseEvt);
                    SetEvent(g_mouseChangedEvt);
                    return 1; // generated clicks are sent at the selected rate instead
                }
            } else if (message == WM_LBUTTONUP) {
                bool swallow = g_swallowPhysicalUp.exchange(false);
                g_mouseHeld = false;
                ResetEvent(g_mouseEvt);
                SetEvent(g_mouseChangedEvt);
                if (swallow) return 1;
            }
        }
    }
    return CallNextHookEx(g_mouseHook, code, message, data);
}
static void SendLeftClick() {
    INPUT in[2] = {};
    in[0].type = INPUT_MOUSE; in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].type = INPUT_MOUSE; in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, in, sizeof(INPUT));
}
static DWORD WINAPI ClickThread(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    for (;;) {
        WaitForSingleObject(g_runEvt, INFINITE);
        if (g_quit) break;
        while (g_running && !g_quit) {
            if (!g_mouseHeld) {
                HANDLE waits[] = { g_mouseEvt, g_stopEvt };
                WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                continue;
            }

            LARGE_INTEGER now; QueryPerformanceCounter(&now);
            long long last = now.QuadPart;
            while (g_running && g_mouseHeld && !g_quit) {
                if (!CursorOverOwnWindow()) { SendLeftClick(); g_clicks++; }
                int cps = g_cps;
                long long interval = f.QuadPart / cps;
                long long next = last + interval;
                for (;;) {
                    if (!g_running || !g_mouseHeld || g_quit) break;
                    if (g_cps != cps) {
                        cps = g_cps;
                        interval = f.QuadPart / cps;
                        next = last + interval;
                    }
                    QueryPerformanceCounter(&now);
                    long long remain = next - now.QuadPart;
                    if (remain <= 0) break;
                    double ms = remain * 1000.0 / (double)f.QuadPart;
                    if (ms > 2.0) {
                        DWORD timeout = (DWORD)(ms - 1.5 > 25.0 ? 25.0 : ms - 1.5);
                        HANDLE waits[] = { g_mouseChangedEvt, g_stopEvt };
                        WaitForMultipleObjects(2, waits, FALSE, timeout);
                    } else YieldProcessor();
                }
                QueryPerformanceCounter(&now);
                last = (now.QuadPart - next > interval * 2) ? now.QuadPart : next; // no burst catch-up
            }
        }
    }
    return 0;
}

// ---------- Drawing helpers ----------
static void RoundRect(HDC dc, RECT r, int rad, COLORREF fill, COLORREF border) {
    HBRUSH b = CreateSolidBrush(fill); HPEN p = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(b); DeleteObject(p);
}
static void Text(HDC dc, const wchar_t* s, RECT r, HFONT f, COLORREF c, UINT fmt) {
    SelectObject(dc, f); SetTextColor(dc, c); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, &r, fmt | DT_NOPREFIX);
}
static RECT R(int l, int t, int w, int h) { RECT r = { S(l), S(t), S(l + w), S(t + h) }; return r; }

static void Paint(HDC dc) {
    RECT cr; GetClientRect(g_main, &cr);
    HDC m = CreateCompatibleDC(dc);
    HBITMAP bm = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ obm = SelectObject(m, bm);
    FillRect(m, &cr, g_brBg);

    bool run = g_running;
    bool clicking = run && g_mouseHeld;

    // Brand header
    RoundRect(m, R(24, 16, 712, 58), S(16), C_PANEL, C_BORDER);
    RECT logo = R(42, 30, 28, 28);
    HBRUSH lb = CreateSolidBrush(C_ACCENT);
    HGDIOBJ oldBrush = SelectObject(m, lb);
    HPEN oldPen = (HPEN)SelectObject(m, GetStockObject(NULL_PEN));
    Ellipse(m, logo.left, logo.top, logo.right, logo.bottom);
    SelectObject(m, oldBrush); SelectObject(m, oldPen); DeleteObject(lb);
    Text(m, L"R", R(42, 29, 28, 28), g_fBold, C_BG, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Text(m, L"CPS BY RAFU", R(82, 22, 330, 30), g_fTitle, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Text(m, L"CLICK PROFILE STUDIO  /  DEVELOPED BY RAFU", R(84, 50, 400, 16), g_fSmall, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    COLORREF stateColor = clicking ? C_GREEN : (run ? C_ACCENT : C_MUTED);
    COLORREF stateFill = clicking ? RGB(15, 48, 42) : (run ? RGB(20, 39, 58) : C_PANEL2);
    RoundRect(m, R(565, 27, 150, 36), S(12), stateFill, run ? stateColor : C_BORDER);
    RECT dot = R(578, 39, 10, 10);
    HBRUSH db = CreateSolidBrush(stateColor); oldBrush = SelectObject(m, db);
    oldPen = (HPEN)SelectObject(m, GetStockObject(NULL_PEN));
    Ellipse(m, dot.left, dot.top, dot.right, dot.bottom);
    SelectObject(m, oldBrush); SelectObject(m, oldPen); DeleteObject(db);
    const wchar_t* state = clicking ? L"CLICKING" : (run ? L"ARMED" : L"READY");
    Text(m, state, R(596, 27, 106, 36), g_fBold, stateColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // CPS selector card
    RoundRect(m, R(24, 88, 448, 340), S(18), C_PANEL, C_BORDER);
    Text(m, L"CLICK RATE", R(48, 106, 240, 24), g_fBold, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Text(m, L"Choose a speed from 1 to 60 CPS", R(48, 132, 350, 18), g_fSmall, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    wchar_t num[16]; swprintf(num, 16, L"%d", g_cps.load());
    Text(m, num, R(48, 150, 220, 84), g_fBig, C_ACCENT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Text(m, L"CPS", R(274, 184, 90, 30), g_fTitle, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Text(m, L"1",  R(48, 236, 40, 16), g_fSmall, C_MUTED, DT_LEFT | DT_SINGLELINE);
    Text(m, L"60", R(408, 236, 40, 16), g_fSmall, C_MUTED, DT_RIGHT | DT_SINGLELINE);

    RoundRect(m, R(490, 88, 246, 340), S(18), C_PANEL, C_BORDER);
    Text(m, L"LIVE SESSION", R(512, 106, 202, 24), g_fBold, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Text(m, L"TOTAL CLICKS", R(512, 154, 202, 18), g_fSmall, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    wchar_t cnt[64]; swprintf(cnt, 64, L"%lld", g_clicks.load());
    Text(m, cnt, R(512, 174, 202, 54), g_fBig, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Text(m, clicking ? L"Left button is held" : (run ? L"Hold left button to click" : L"Clicker is paused"),
         R(512, 237, 202, 22), g_fSmall, clicking ? C_GREEN : C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Text(m, L"Start arms the clicker. Release the mouse to pause.",
         R(512, 264, 202, 42), g_fSmall, C_MUTED, DT_LEFT | DT_WORDBREAK);

    // Quick CPS presets
    Text(m, L"QUICK SELECT", R(48, 286, 200, 18), g_fSmall, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Text(m, L"SET CPS", R(512, 314, 202, 18), g_fSmall, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Hotkey dock
    RoundRect(m, R(24, 446, 712, 138), S(18), C_PANEL, C_BORDER);
    Text(m, L"SHORTCUTS", R(44, 460, 160, 22), g_fBold, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Text(m, L"Start", R(44, 504, 62, 24), g_fSmall, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Text(m, L"Stop", R(282, 504, 56, 24), g_fSmall, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Text(m, L"Emergency", R(502, 504, 78, 24), g_fSmall, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Text(m, L"Defaults: F6 start  /  F7 stop  /  F8 emergency stop", R(44, 548, 660, 20),
         g_fSmall, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    bool showMsg = g_msg[0] && (int)(g_msgUntil - GetTickCount()) > 0;
    if (showMsg) Text(m, g_msg, R(492, 396, 234, 24), g_fSmall, RGB(255, 193, 7), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    BitBlt(dc, 0, 0, cr.right, cr.bottom, m, 0, 0, SRCCOPY);
    SelectObject(m, obm); DeleteObject(bm); DeleteDC(m);
}

static void DrawButton(const DRAWITEMSTRUCT* d) {
    bool start = d->CtlID == IDC_START;
    bool preset = d->CtlID == IDC_PRESET10 || d->CtlID == IDC_PRESET13 || d->CtlID == IDC_PRESET17;
    bool selected = (d->CtlID == IDC_PRESET10 && g_cps == 10) ||
                    (d->CtlID == IDC_PRESET13 && g_cps == 13) ||
                    (d->CtlID == IDC_PRESET17 && g_cps == 17);
    bool dis = (d->itemState & ODS_DISABLED) != 0;
    bool down = (d->itemState & ODS_SELECTED) != 0;
    COLORREF base = preset ? C_ACCENT : (start ? C_GREEN : C_RED);
    COLORREF fill = preset ? (selected ? C_ACCENT : C_PANEL2) : base;
    COLORREF border = preset ? (selected ? C_ACCENT : C_BORDER) : base;
    COLORREF txt = preset ? (selected ? C_BG : C_TEXT) : C_BG;
    if (dis) { fill = C_PANEL2; border = C_BORDER; txt = C_MUTED; }
    else if (down && !preset) fill = RGB(GetRValue(base) * 3 / 4, GetGValue(base) * 3 / 4, GetBValue(base) * 3 / 4);
    FillRect(d->hDC, &d->rcItem, g_brBg);
    RoundRect(d->hDC, d->rcItem, S(12), fill, border);
    const wchar_t* label = start ? L"START" : (preset ? (d->CtlID == IDC_PRESET10 ? L"10 CPS" : (d->CtlID == IDC_PRESET13 ? L"13 CPS" : L"17 CPS")) : L"STOP");
    Text(d->hDC, label, d->rcItem, g_fBold, txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static LRESULT SliderCustomDraw(LPNMCUSTOMDRAW cd) {
    switch (cd->dwDrawStage) {
    case CDDS_PREPAINT:
        FillRect(cd->hdc, &cd->rc, g_brBg);
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
        if (cd->dwItemSpec == TBCD_CHANNEL) {
            int cy = (cd->rc.top + cd->rc.bottom) / 2, h = S(3);
            RECT tr = { cd->rc.left, cy - h, cd->rc.right, cy + h };
            RoundRect(cd->hdc, tr, S(6), C_PANEL, C_BORDER);
            int pos = (int)SendMessageW(g_slider, TBM_GETPOS, 0, 0);
            int x = tr.left + (int)((tr.right - tr.left) * (pos - 1) / 59.0);
            RECT fr = { tr.left + 1, tr.top + 1, x < tr.left + S(8) ? tr.left + S(8) : x, tr.bottom - 1 };
            HBRUSH b = CreateSolidBrush(C_ACCENT); FillRect(cd->hdc, &fr, b); DeleteObject(b);
            return CDRF_SKIPDEFAULT;
        }
        if (cd->dwItemSpec == TBCD_THUMB) {
            RECT t = cd->rc; InflateRect(&t, -S(1), 0);
            RoundRect(cd->hdc, t, S(6), C_TEXT, C_ACCENT);
            return CDRF_SKIPDEFAULT;
        }
        if (cd->dwItemSpec == TBCD_TICS) return CDRF_SKIPDEFAULT;
        break;
    }
    return CDRF_DODEFAULT;
}

// ---------- Window ----------
static HFONT MkFont(int px, int weight) {
    return CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

static void OnHotkeyChanged(int ctlId) {
    HWND h = ctlId == IDC_HK_START ? g_hkStart : (ctlId == IDC_HK_STOP ? g_hkStop : g_hkEstop);
    WORD v = (WORD)SendMessageW(h, HKM_GETHOTKEY, 0, 0);
    if (!LOBYTE(v)) return;                       // modifier only so far - wait for a real key
    int id;
    WORD& cur = ctlId == IDC_HK_START ? g_hkStartVal : (ctlId == IDC_HK_STOP ? g_hkStopVal : g_hkEstopVal);
    WORD otherA, otherB;
    if (ctlId == IDC_HK_START) {
        id = HK_START; otherA = g_hkStopVal; otherB = g_hkEstopVal;
    } else if (ctlId == IDC_HK_STOP) {
        id = HK_STOP; otherA = g_hkStartVal; otherB = g_hkEstopVal;
    } else {
        id = HK_ESTOP; otherA = g_hkStartVal; otherB = g_hkStopVal;
    }
    if (v == cur) return;
    if (v == otherA || v == otherB) { SetMsg(L"Each hotkey must be different."); SyncHotkeyControls(); return; }
    if (!RegisterOne(id, v)) {
        RegisterOne(id, cur);
        SetMsg(L"That key combination is already used by another program.");
        SyncHotkeyControls(); return;
    }
    cur = v; SaveSettings(); SetMsg(L"Hotkey updated.", 2000);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_main = h;
        BOOL dark = TRUE; DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        g_fTitle = MkFont(20, FW_BOLD); g_fBig = MkFont(58, FW_BOLD); g_fNorm = MkFont(14, FW_NORMAL);
        g_fSmall = MkFont(11, FW_NORMAL); g_fBold = MkFont(14, FW_BOLD);
        g_brBg = CreateSolidBrush(C_BG);

        RECT r = R(48, 250, 400, 32);
        g_slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
            r.left, r.top, r.right - r.left, r.bottom - r.top, h, (HMENU)IDC_SLIDER, nullptr, nullptr);
        SendMessageW(g_slider, TBM_SETRANGE, TRUE, MAKELONG(1, 60));
        SendMessageW(g_slider, TBM_SETPAGESIZE, 0, 5);
        SendMessageW(g_slider, TBM_SETLINESIZE, 0, 1);
        SendMessageW(g_slider, TBM_SETPOS, TRUE, g_cps.load());

        r = R(510, 350, 95, 44);
        g_btnStart = CreateWindowExW(0, L"BUTTON", L"START", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            r.left, r.top, r.right - r.left, r.bottom - r.top, h, (HMENU)IDC_START, nullptr, nullptr);
        r = R(617, 350, 95, 44);
        g_btnStop = CreateWindowExW(0, L"BUTTON", L"STOP", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            r.left, r.top, r.right - r.left, r.bottom - r.top, h, (HMENU)IDC_STOP, nullptr, nullptr);

        r = R(48, 310, 116, 44);
        g_btn10 = CreateWindowExW(0, L"BUTTON", L"10 CPS", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            r.left, r.top, r.right - r.left, r.bottom - r.top, h, (HMENU)IDC_PRESET10, nullptr, nullptr);
        r = R(176, 310, 116, 44);
        g_btn13 = CreateWindowExW(0, L"BUTTON", L"13 CPS", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            r.left, r.top, r.right - r.left, r.bottom - r.top, h, (HMENU)IDC_PRESET13, nullptr, nullptr);
        r = R(304, 310, 116, 44);
        g_btn17 = CreateWindowExW(0, L"BUTTON", L"17 CPS", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            r.left, r.top, r.right - r.left, r.bottom - r.top, h, (HMENU)IDC_PRESET17, nullptr, nullptr);

        r = R(110, 498, 142, 28);
        g_hkStart = CreateWindowExW(0, HOTKEY_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            r.left, r.top, r.right - r.left, r.bottom - r.top, h, (HMENU)IDC_HK_START, nullptr, nullptr);
        r = R(340, 498, 130, 28);
        g_hkStop = CreateWindowExW(0, HOTKEY_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            r.left, r.top, r.right - r.left, r.bottom - r.top, h, (HMENU)IDC_HK_STOP, nullptr, nullptr);
        r = R(580, 498, 132, 28);
        g_hkEstop = CreateWindowExW(0, HOTKEY_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            r.left, r.top, r.right - r.left, r.bottom - r.top, h, (HMENU)IDC_HK_ESTOP, nullptr, nullptr);
        SendMessageW(g_hkStart, WM_SETFONT, (WPARAM)g_fNorm, TRUE);
        SendMessageW(g_hkStop,  WM_SETFONT, (WPARAM)g_fNorm, TRUE);
        SendMessageW(g_hkEstop,  WM_SETFONT, (WPARAM)g_fNorm, TRUE);
        SyncHotkeyControls();

        bool hotkeysOk = RegisterOne(HK_START, g_hkStartVal);
        hotkeysOk = RegisterOne(HK_STOP, g_hkStopVal) && hotkeysOk;
        hotkeysOk = RegisterOne(HK_ESTOP, g_hkEstopVal) && hotkeysOk;
        if (!hotkeysOk)
            SetMsg(L"A default hotkey is taken by another program - pick a different key.", 8000);
        UpdateUI();
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); Paint(dc); EndPaint(h, &ps); return 0; }
    case WM_DRAWITEM: DrawButton((DRAWITEMSTRUCT*)l); return TRUE;
    case WM_NOTIFY: {
        NMHDR* n = (NMHDR*)l;
        if (n->idFrom == IDC_SLIDER && n->code == NM_CUSTOMDRAW) return SliderCustomDraw((LPNMCUSTOMDRAW)l);
        break;
    }
    case WM_HSCROLL:
        if ((HWND)l == g_slider) {
            g_cps = (int)SendMessageW(g_slider, TBM_GETPOS, 0, 0);
            InvalidateRect(g_slider, nullptr, TRUE);
            UpdateUI();
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_START: SetRunning(true);  return 0;
        case IDC_STOP:  SetRunning(false); return 0;
        case IDC_PRESET10: g_cps = 10; SendMessageW(g_slider, TBM_SETPOS, TRUE, 10); UpdateUI(); return 0;
        case IDC_PRESET13: g_cps = 13; SendMessageW(g_slider, TBM_SETPOS, TRUE, 13); UpdateUI(); return 0;
        case IDC_PRESET17: g_cps = 17; SendMessageW(g_slider, TBM_SETPOS, TRUE, 17); UpdateUI(); return 0;
        case IDC_HK_START: case IDC_HK_STOP: case IDC_HK_ESTOP:
            if (HIWORD(w) == EN_CHANGE) OnHotkeyChanged(LOWORD(w));
            return 0;
        }
        break;
    case WM_HOTKEY:
        if (w == HK_START) SetRunning(true);
        else if (w == HK_STOP) SetRunning(false);
        else if (w == HK_ESTOP) { bool was = g_running; SetRunning(false); if (was) SetMsg(L"Emergency stop - clicking halted."); }
        return 0;
    case WM_TIMER: InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_CLOSE: SetRunning(false); DestroyWindow(h); return 0;
    case WM_DESTROY:
        SaveSettings();
        if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = nullptr; }
        UnregisterHotKey(h, HK_START); UnregisterHotKey(h, HK_STOP); UnregisterHotKey(h, HK_ESTOP);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    HDC sdc = GetDC(nullptr); g_dpi = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(nullptr, sdc);
    INITCOMMONCONTROLSEX ic = { sizeof(ic), ICC_BAR_CLASSES | ICC_HOTKEY_CLASS };
    InitCommonControlsEx(&ic);
    timeBeginPeriod(1);
    IniPath(); LoadSettings();

    g_runEvt  = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_stopEvt = CreateEventW(nullptr, TRUE, TRUE,  nullptr);
    g_mouseEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_mouseChangedEvt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_thread  = CreateThread(nullptr, 0, ClickThread, nullptr, 0, nullptr);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = L"CPSByRafuWnd";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    wc.hIconSm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = { 0, 0, S(760), S(630) };
    AdjustWindowRect(&rc, style, FALSE);
    HWND hw = CreateWindowExW(0, wc.lpszClassName, L"CPS by Rafu", style, CW_USEDEFAULT, CW_USEDEFAULT,
                              rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hi, nullptr);
    if (!hw) return 1;
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, hi, 0);
    ShowWindow(hw, show); UpdateWindow(hw);
    if (!g_mouseHook) SetMsg(L"Could not install the mouse listener; click mode is unavailable.", 8000);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hw, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    g_quit = true; g_running = false;
    g_mouseHeld = false;
    SetEvent(g_runEvt); SetEvent(g_stopEvt); SetEvent(g_mouseChangedEvt);
    WaitForSingleObject(g_thread, 1000);
    if (g_thread) CloseHandle(g_thread);
    if (g_runEvt) CloseHandle(g_runEvt);
    if (g_stopEvt) CloseHandle(g_stopEvt);
    if (g_mouseEvt) CloseHandle(g_mouseEvt);
    if (g_mouseChangedEvt) CloseHandle(g_mouseChangedEvt);
    timeEndPeriod(1);
    return 0;
}
