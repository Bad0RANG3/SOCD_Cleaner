/**
 * SOCD Cleaner — WASD 键盘 SOCD 裁决工具
 *
 * 拦截 W/A/S/D 物理按键，裁决冲突键对（W↔S 纵轴、A↔D 横轴），
 * 注入清洗后的虚拟按键。系统托盘运行，右键切换模式。
 *
 * 三种 SOCD 模式：
 *   回中    — 冲突键抵消，无输出（赛事标准）
 *   后发优先 — 后按的键生效（FPS 急停）
 *   先发优先 — 先按的键生效
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include "socd_engine.h"

// ─── 常量 ──────────────────────────────────────────────────────────────────

constexpr const wchar_t* WND_CLASS   = L"SOCDCleaner";
constexpr const wchar_t* WND_TITLE   = L"SOCD Cleaner";
constexpr UINT  WM_TRAY    = WM_APP + 1;
constexpr UINT  TRAY_ID    = 1;

constexpr ULONG_PTR SOCD_MAGIC = 0x534F43440001ULL;  // SendInput 自标记

enum { IDM_EXIT = 1001, IDM_NEUTRAL = 2001, IDM_LAST = 2002, IDM_FIRST = 2003,
       IDM_ABOUT = 4001 };

// ─── 全局状态 ──────────────────────────────────────────────────────────────

static HINSTANCE  g_hInst;
static SocdEngine g_engine(SocdMode::LastWin);
static HHOOK      g_hHook;
static HMENU      g_hMenu;
static SocdOutput g_lastOut;        // 差量发送用

// ─── 前向声明 ──────────────────────────────────────────────────────────────

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK KeyboardHook(int, WPARAM, LPARAM);
void TrayAdd(HWND), TrayDel(HWND), TrayTip(HWND), MenuShow(HWND);
void SendKey(int vk, bool press);
void ApplyOutput(const SocdOutput& out);

// ─── 入口 ──────────────────────────────────────────────────────────────────

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    g_hInst = hInst;

    // 单实例
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Global\\SOCDCleaner");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"SOCD Cleaner 已在运行。", WND_TITLE,
                    MB_OK | MB_ICONINFORMATION);
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    // 注册窗口类
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = WND_CLASS;
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);

    // 创建隐藏窗口
    HWND hwnd = CreateWindowExW(0, WND_CLASS, WND_TITLE, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
                                nullptr, nullptr, hInst, nullptr);
    // 设置窗口图标（Alt+Tab / 任务栏）
    SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)LoadIconW(nullptr, IDI_APPLICATION));
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(nullptr, IDI_APPLICATION));

    ShowWindow(hwnd, SW_HIDE);
    TrayAdd(hwnd);

    // 低级键盘钩子
    g_hHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, hInst, 0);
    if (!g_hHook) {
        MessageBoxW(nullptr, L"键盘钩子安装失败，请以管理员运行。",
                    WND_TITLE, MB_OK | MB_ICONERROR);
        TrayDel(hwnd); DestroyWindow(hwnd);
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    // 消息循环
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 清理
    ApplyOutput(SocdOutput{});
    if (g_hHook) { UnhookWindowsHookEx(g_hHook); g_hHook = nullptr; }
    TrayDel(hwnd); DestroyWindow(hwnd);
    if (hMutex) CloseHandle(hMutex);
    return static_cast<int>(msg.wParam);
}

// ─── 窗口过程 ──────────────────────────────────────────────────────────────

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_CREATE: {
        g_hMenu = CreatePopupMenu();
        HMENU hMode = CreatePopupMenu();
        AppendMenuW(hMode, MF_STRING, IDM_NEUTRAL, L"回中 (Neutral)");
        AppendMenuW(hMode, MF_STRING, IDM_LAST,    L"后发优先 (Last Win)");
        AppendMenuW(hMode, MF_STRING, IDM_FIRST,   L"先发优先 (First Win)");
        CheckMenuRadioItem(hMode, IDM_NEUTRAL, IDM_FIRST, IDM_LAST, MF_BYCOMMAND);

        AppendMenuW(g_hMenu, MF_POPUP, (UINT_PTR)hMode, L"SOCD 模式");
        AppendMenuW(g_hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(g_hMenu, MF_STRING, IDM_ABOUT, L"关于");
        AppendMenuW(g_hMenu, MF_STRING, IDM_EXIT,  L"退出");
        return 0;
    }

    case WM_DESTROY:
        if (g_hMenu) { DestroyMenu(g_hMenu); g_hMenu = nullptr; }
        PostQuitMessage(0);
        return 0;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP) MenuShow(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_EXIT: DestroyWindow(hwnd); return 0;
        case IDM_NEUTRAL: g_engine.SetMode(SocdMode::Neutral);  goto reset;
        case IDM_LAST:    g_engine.SetMode(SocdMode::LastWin);  goto reset;
        case IDM_FIRST:   g_engine.SetMode(SocdMode::FirstWin); goto reset;
        reset:
            g_engine.ResetAllKeys();
            ApplyOutput(SocdOutput{});
            TrayTip(hwnd);
            return 0;
        case IDM_ABOUT:
            MessageBoxW(hwnd,
                L"SOCD Cleaner\n\n"
                L"WASD 键盘 SOCD 裁决工具。拦截 W/A/S/D，处理相反方向冲突。\n\n"
                L"  回中    — A+D 或 W+S 同时按时双方抵消\n"
                L"  后发优先 — 后按的键覆盖先按的键\n"
                L"  先发优先 — 先按的键锁定，后按无效\n\n"
                L"右键托盘图标切换模式。",
                WND_TITLE, MB_OK | MB_ICONINFORMATION);
            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ─── 系统托盘 ──────────────────────────────────────────────────────────────

void TrayAdd(HWND hwnd) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hwnd;
    nid.uID              = TRAY_ID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon            = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"SOCD Cleaner — 后发优先");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void TrayDel(HWND hwnd) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid); nid.hWnd = hwnd; nid.uID = TRAY_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void TrayTip(HWND hwnd) {
    const wchar_t* mode = L"后发优先";
    switch (g_engine.GetMode()) {
        case SocdMode::Neutral: mode = L"回中";    break;
        case SocdMode::LastWin: mode = L"后发优先"; break;
        case SocdMode::FirstWin: mode = L"先发优先"; break;
    }
    wchar_t tip[128];
    swprintf_s(tip, L"SOCD Cleaner — %s", mode);

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid); nid.hWnd = hwnd; nid.uID = TRAY_ID;
    nid.uFlags = NIF_TIP; wcscpy_s(nid.szTip, tip);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void MenuShow(HWND hwnd) {
    HMENU hMode = GetSubMenu(g_hMenu, 0);
    CheckMenuRadioItem(hMode, IDM_NEUTRAL, IDM_FIRST,
        g_engine.GetMode() == SocdMode::Neutral  ? IDM_NEUTRAL :
        g_engine.GetMode() == SocdMode::LastWin  ? IDM_LAST : IDM_FIRST,
        MF_BYCOMMAND);

    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenuEx(g_hMenu,
                     TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON,
                     pt.x, pt.y, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
}

// ─── 虚拟按键注入 ──────────────────────────────────────────────────────────

void SendKey(int vk, bool press) {
    INPUT i = {};
    i.type           = INPUT_KEYBOARD;
    i.ki.wVk         = static_cast<WORD>(vk);
    i.ki.dwFlags     = press ? 0 : KEYEVENTF_KEYUP;
    i.ki.dwExtraInfo = SOCD_MAGIC;
    SendInput(1, &i, sizeof(i));
}

void ApplyOutput(const SocdOutput& out) {
    auto apply = [](int cur, int& prev) {
        if (cur != prev) {
            if (prev) SendKey(prev, false);
            if (cur)  SendKey(cur,  true);
            prev = cur;
        }
    };
    apply(out.vkW, g_lastOut.vkW);
    apply(out.vkA, g_lastOut.vkA);
    apply(out.vkS, g_lastOut.vkS);
    apply(out.vkD, g_lastOut.vkD);
}

// ─── 键盘钩子 ──────────────────────────────────────────────────────────────

LRESULT CALLBACK KeyboardHook(int nCode, WPARAM wp, LPARAM lp) {
    if (nCode < 0)
        return CallNextHookEx(nullptr, nCode, wp, lp);

    auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);

    // 放行注入事件（自己的 + 其他程序的）
    if (kb->dwExtraInfo == SOCD_MAGIC || (kb->flags & LLKHF_INJECTED))
        return CallNextHookEx(nullptr, nCode, wp, lp);

    bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
    bool up   = (wp == WM_KEYUP   || wp == WM_SYSKEYUP);
    if (!down && !up)
        return CallNextHookEx(nullptr, nCode, wp, lp);

    // 仅 WASD，其余透传
    int vk = static_cast<int>(kb->vkCode);
    if (!SocdEngine::IsWasdKey(vk))
        return CallNextHookEx(nullptr, nCode, wp, lp);

    ApplyOutput(g_engine.ProcessKey(vk, down));
    return 1;  // 拦截原始按键
}
