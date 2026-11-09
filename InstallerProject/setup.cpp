#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <commctrl.h>
#include <strsafe.h>
#include <string>
#include <vector>
#include "setup_resource.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uuid.lib")

namespace {
constexpr wchar_t kProduct[] = L"CPS by Rafu";
constexpr wchar_t kRegistryKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CPSByRafu";
constexpr int kWindowW = 720;
constexpr int kWindowH = 520;

HWND g_window = nullptr;
bool g_desktopShortcut = true;
bool g_launchAfterInstall = true;
bool g_working = false;
std::wstring g_status = L"Ready to install";

std::wstring Join(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &raw)) || !raw) return {};
    std::wstring value(raw);
    CoTaskMemFree(raw);
    return value;
}

std::wstring InstallDir() {
    auto local = KnownFolder(FOLDERID_LocalAppData);
    return local.empty() ? std::wstring() : Join(Join(Join(local, L"Programs"), kProduct), L"");
}

bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
}

bool WriteResourceToFile(int id, const wchar_t* type, const std::wstring& filePath) {
    HRSRC resource = FindResourceW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(id), type);
    if (!resource) return false;
    HGLOBAL loaded = LoadResource(GetModuleHandleW(nullptr), resource);
    if (!loaded) return false;
    const DWORD size = SizeofResource(GetModuleHandleW(nullptr), resource);
    const void* bytes = LockResource(loaded);
    if (!bytes || size == 0) return false;
    HANDLE file = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes, size, &written, nullptr);
    CloseHandle(file);
    return ok && written == size;
}

bool CopyCurrentModule(const std::wstring& destination) {
    wchar_t module[MAX_PATH]{};
    const DWORD count = GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (!count || count >= MAX_PATH) return false;
    return CopyFileW(module, destination.c_str(), FALSE) != FALSE;
}

bool SetRegString(HKEY key, const wchar_t* name, const std::wstring& value) {
    return RegSetValueExW(key, name, 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool WriteUninstallRegistration(const std::wstring& dir) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const auto app = Join(dir, L"CPS by Rafu.exe");
    const auto uninstaller = Join(dir, L"uninstall.exe");
    std::wstring uninstallCommand = L"\"" + uninstaller + L"\" --uninstall";
    bool ok = SetRegString(key, L"DisplayName", kProduct) &&
              SetRegString(key, L"DisplayVersion", L"1.0.0") &&
              SetRegString(key, L"Publisher", L"Rafu") &&
              SetRegString(key, L"InstallLocation", dir) &&
              SetRegString(key, L"DisplayIcon", app) &&
              SetRegString(key, L"UninstallString", uninstallCommand) &&
              SetRegString(key, L"QuietUninstallString", uninstallCommand);
    DWORD yes = 1;
    ok = ok && RegSetValueExW(key, L"NoModify", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&yes), sizeof(yes)) == ERROR_SUCCESS;
    ok = ok && RegSetValueExW(key, L"NoRepair", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&yes), sizeof(yes)) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}

bool CreateShortcut(const std::wstring& linkPath, const std::wstring& target,
                    const std::wstring& args, const std::wstring& workDir,
                    const std::wstring& description) {
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&link));
    if (FAILED(hr)) return false;
    link->SetPath(target.c_str());
    link->SetArguments(args.c_str());
    link->SetWorkingDirectory(workDir.c_str());
    link->SetDescription(description.c_str());
    link->SetIconLocation(target.c_str(), 0);
    IPersistFile* persist = nullptr;
    hr = link->QueryInterface(IID_PPV_ARGS(&persist));
    if (SUCCEEDED(hr)) {
        hr = persist->Save(linkPath.c_str(), TRUE);
        persist->Release();
    }
    link->Release();
    return SUCCEEDED(hr);
}

bool InstallProduct() {
    const std::wstring dir = InstallDir();
    if (dir.empty() || !EnsureDirectory(dir)) return false;
    const auto app = Join(dir, L"CPS by Rafu.exe");
    const auto readme = Join(dir, L"README.txt");
    const auto uninstaller = Join(dir, L"uninstall.exe");
    if (!WriteResourceToFile(IDR_CPS_EXE, RT_RCDATA, app) ||
        !WriteResourceToFile(IDR_README, RT_RCDATA, readme) ||
        !CopyCurrentModule(uninstaller)) return false;

    const std::wstring roaming = KnownFolder(FOLDERID_RoamingAppData);
    const std::wstring programs = roaming.empty() ? L"" : Join(Join(roaming, L"Microsoft\\Windows\\Start Menu"), L"Programs");
    const std::wstring menuDir = programs.empty() ? L"" : Join(programs, kProduct);
    if (menuDir.empty() || !EnsureDirectory(menuDir)) return false;

    const auto appLink = Join(menuDir, L"CPS by Rafu.lnk");
    const auto uninstallLink = Join(menuDir, L"Uninstall CPS by Rafu.lnk");
    if (!CreateShortcut(appLink, app, L"", dir, kProduct) ||
        !CreateShortcut(uninstallLink, uninstaller, L"--uninstall", dir, L"Uninstall CPS by Rafu")) return false;

    if (g_desktopShortcut) {
        const auto desktop = KnownFolder(FOLDERID_Desktop);
        if (!desktop.empty() && !CreateShortcut(Join(desktop, L"CPS by Rafu.lnk"), app, L"", dir, kProduct)) return false;
    }
    return WriteUninstallRegistration(dir);
}

std::wstring EscapePowerShellSingleQuote(const std::wstring& value) {
    std::wstring escaped;
    for (wchar_t c : value) {
        escaped.push_back(c);
        if (c == L'\'') escaped.push_back(c);
    }
    return escaped;
}

void LaunchCleanup(const std::wstring& installDir) {
    wchar_t temp[MAX_PATH]{};
    const DWORD count = GetTempPathW(MAX_PATH, temp);
    if (!count || count >= MAX_PATH) return;
    wchar_t scriptPath[MAX_PATH]{};
    StringCchPrintfW(scriptPath, MAX_PATH, L"%sCPSByRafuCleanup-%lu.ps1", temp, GetCurrentProcessId());
    const std::wstring script = L"\xFEFFStart-Sleep -Seconds 2\n$target = '" + EscapePowerShellSingleQuote(installDir) +
        L"'\nRemove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue\n";
    HANDLE file = CreateFileW(scriptPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, script.data(), static_cast<DWORD>(script.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
    if (written != script.size() * sizeof(wchar_t)) return;
    std::wstring args = std::wstring(L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"") +
        scriptPath + L"\"";
    ShellExecuteW(nullptr, L"open", L"powershell.exe", args.c_str(), nullptr, SW_HIDE);
}

std::wstring CurrentModuleDirectory() {
    wchar_t path[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!len || len >= MAX_PATH) return {};
    std::wstring result(path, len);
    const auto slash = result.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    result.resize(slash);
    return result;
}

int RunUninstaller() {
    const std::wstring dir = CurrentModuleDirectory();
    if (dir.empty()) return 1;
    if (MessageBoxW(nullptr, L"Remove CPS by Rafu and its shortcuts from this PC?",
                    L"Uninstall CPS by Rafu", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) return 0;
    const auto roaming = KnownFolder(FOLDERID_RoamingAppData);
    if (!roaming.empty()) {
        const auto menuDir = Join(Join(Join(roaming, L"Microsoft\\Windows\\Start Menu"), L"Programs"), kProduct);
        DeleteFileW(Join(menuDir, L"CPS by Rafu.lnk").c_str());
        DeleteFileW(Join(menuDir, L"Uninstall CPS by Rafu.lnk").c_str());
        RemoveDirectoryW(menuDir.c_str());
    }
    const auto desktop = KnownFolder(FOLDERID_Desktop);
    if (!desktop.empty()) DeleteFileW(Join(desktop, L"CPS by Rafu.lnk").c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, kRegistryKey);
    LaunchCleanup(dir);
    MessageBoxW(nullptr, L"CPS by Rafu has been removed.", L"Uninstall complete", MB_ICONINFORMATION | MB_OK);
    return 0;
}

void Text(HDC dc, int x, int y, const wchar_t* value, COLORREF color, int size,
          int weight = FW_NORMAL, const wchar_t* face = L"Segoe UI") {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    HFONT font = CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
    HGDIOBJ old = SelectObject(dc, font);
    TextOutW(dc, x, y, value, static_cast<int>(wcslen(value)));
    SelectObject(dc, old);
    DeleteObject(font);
}

void Rounded(HDC dc, RECT rect, COLORREF fill, COLORREF border, int radius = 12) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    auto oldBrush = SelectObject(dc, brush);
    auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawCheck(HDC dc, int x, int y, bool checked) {
    RECT box{x, y, x + 20, y + 20};
    Rounded(dc, box, checked ? RGB(61, 218, 190) : RGB(24, 34, 51), RGB(67, 88, 112), 5);
    if (checked) {
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(9, 22, 32));
        auto old = SelectObject(dc, pen);
        MoveToEx(dc, x + 5, y + 10, nullptr); LineTo(dc, x + 9, y + 14); LineTo(dc, x + 16, y + 6);
        SelectObject(dc, old); DeleteObject(pen);
    }
}

void Paint(HDC dc) {
    RECT client{};
    GetClientRect(g_window, &client);
    HBRUSH bg = CreateSolidBrush(RGB(10, 15, 25));
    FillRect(dc, &client, bg);
    DeleteObject(bg);

    RECT hero{0, 0, kWindowW, 218};
    HBRUSH heroBrush = CreateSolidBrush(RGB(17, 27, 44));
    FillRect(dc, &hero, heroBrush);
    DeleteObject(heroBrush);
    RECT stripe{0, 0, 7, 218};
    HBRUSH accent = CreateSolidBrush(RGB(61, 218, 190));
    FillRect(dc, &stripe, accent);
    DeleteObject(accent);

    Text(dc, 42, 34, L"RAFU  /  DESKTOP TOOLS", RGB(93, 222, 199), 12, FW_BOLD);
    Text(dc, 42, 66, L"CPS by Rafu", RGB(241, 246, 252), 32, FW_BOLD);
    Text(dc, 44, 116, L"A cleaner way to set up your click speed utility.", RGB(164, 180, 199), 15);
    Rounded(dc, RECT{42, 158, 184, 190}, RGB(28, 48, 61), RGB(50, 82, 91), 16);
    Text(dc, 56, 166, L"VERSION  1.0.0", RGB(144, 229, 213), 11, FW_BOLD);

    Text(dc, 42, 249, L"Install location", RGB(232, 239, 247), 16, FW_BOLD);
    Text(dc, 42, 277, L"This app will be installed for your Windows account.", RGB(141, 158, 180), 13);
    RECT path{42, 309, 678, 357};
    Rounded(dc, path, RGB(17, 25, 39), RGB(42, 56, 76), 10);
    Text(dc, 58, 324, InstallDir().c_str(), RGB(198, 212, 229), 13);

    DrawCheck(dc, 44, 382, g_desktopShortcut);
    Text(dc, 75, 383, L"Create a desktop shortcut", RGB(198, 212, 229), 13);
    DrawCheck(dc, 44, 414, g_launchAfterInstall);
    Text(dc, 75, 415, L"Launch CPS by Rafu when setup finishes", RGB(198, 212, 229), 13);

    Text(dc, 42, 468, g_status.c_str(), RGB(133, 156, 181), 12);
    Rounded(dc, RECT{447, 442, 565, 489}, RGB(20, 29, 44), RGB(49, 65, 87), 10);
    Text(dc, 486, 455, L"Cancel", RGB(190, 204, 221), 14, FW_SEMIBOLD);
    Rounded(dc, RECT{578, 442, 678, 489}, g_working ? RGB(37, 70, 75) : RGB(61, 218, 190),
            g_working ? RGB(55, 96, 101) : RGB(61, 218, 190), 10);
    Text(dc, g_working ? 598 : 598, 455, g_working ? L"Working" : L"Install", RGB(9, 24, 33), 14, FW_BOLD);
    Text(dc, 42, 492, L"Developed by Rafu", RGB(80, 103, 129), 11);
}

void InstallFromUi() {
    if (g_working) return;
    g_working = true;
    g_status = L"Installing files and creating shortcuts...";
    InvalidateRect(g_window, nullptr, TRUE);
    UpdateWindow(g_window);
    const bool ok = InstallProduct();
    g_working = false;
    if (!ok) {
        g_status = L"Setup could not finish. Check that the install folder is writable.";
        InvalidateRect(g_window, nullptr, TRUE);
        MessageBoxW(g_window, L"Setup could not complete. Please try again or choose another Windows account.",
                    L"Setup failed", MB_ICONERROR | MB_OK);
        return;
    }
    g_status = L"Installed successfully.";
    InvalidateRect(g_window, nullptr, TRUE);
    if (g_launchAfterInstall) {
        const auto app = Join(InstallDir(), L"CPS by Rafu.exe");
        ShellExecuteW(g_window, L"open", app.c_str(), nullptr, InstallDir().c_str(), SW_SHOWNORMAL);
    }
    MessageBoxW(g_window, L"CPS by Rafu is installed and ready to use.", kProduct, MB_ICONINFORMATION | MB_OK);
    DestroyWindow(g_window);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        Paint(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP: {
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (x >= 38 && x <= 350 && y >= 376 && y <= 407) {
            g_desktopShortcut = !g_desktopShortcut; InvalidateRect(hwnd, nullptr, TRUE); return 0;
        }
        if (x >= 38 && x <= 420 && y >= 408 && y <= 438) {
            g_launchAfterInstall = !g_launchAfterInstall; InvalidateRect(hwnd, nullptr, TRUE); return 0;
        }
        if (x >= 440 && x <= 570 && y >= 435 && y <= 495) { DestroyWindow(hwnd); return 0; }
        if (x >= 575 && x <= 685 && y >= 435 && y <= 495) { InstallFromUi(); return 0; }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        if (wp == VK_RETURN) { InstallFromUi(); return 0; }
        break;
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (commandLine && wcsstr(commandLine, L"--uninstall")) {
        const int result = RunUninstaller();
        CoUninitialize();
        return result;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_SETUP));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(10, 15, 25));
    wc.lpszClassName = L"CPSByRafuSetupWindow";
    if (!RegisterClassExW(&wc)) return 1;
    RECT r{0, 0, kWindowW, kWindowH};
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    const int width = r.right - r.left, height = r.bottom - r.top;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    g_window = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"CPS by Rafu Setup",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, x, y, width, height,
        nullptr, nullptr, instance, nullptr);
    if (!g_window) return 1;
    ShowWindow(g_window, show);
    UpdateWindow(g_window);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
