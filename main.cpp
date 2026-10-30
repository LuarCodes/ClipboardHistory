// ClipboardHistory - remembers everything you copy. Press Ctrl+Shift+V to search and paste old copies.
// (c) 2026 _.luarx - MIT License
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <cwctype>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

#define WM_TRAY (WM_APP + 1)
enum { IDC_SEARCH = 201, IDC_LIST, IDC_HINT,
       ID_OPEN = 301, ID_CLEAR, ID_STARTUP, ID_EXIT,
       HOTKEY_ID = 1, TIMER_PASTE = 1 };

static const size_t MAX_ITEMS = 300;       // unpinned items kept
static const size_t MAX_TEXT  = 200000;    // chars per item

struct Clip { std::wstring text; bool pinned = false; };

static HINSTANCE gInst;
static HWND gMain, gPopup, gSearch, gList, gHint, gPrevFg;
static HFONT gFont, gSmallFont;
static int gDpi = 96;
static std::vector<Clip> gClips;     // newest first
static std::vector<int> gView;       // listbox row -> gClips index
static NOTIFYICONDATAW gNid{};
static UINT WM_TASKBARCREATED;

static int S(int v) { return MulDiv(v, gDpi, 96); }
static std::wstring Lower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }

// ---------- storage ----------
static std::wstring DataPath() {
    wchar_t buf[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf))) return L"history.dat";
    std::wstring dir = std::wstring(buf) + L"\\ClipboardHistory";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\history.dat";
}

static void Save() {
    FILE* f = _wfopen(DataPath().c_str(), L"wb");
    if (!f) return;
    fwrite("CLH1", 1, 4, f);
    for (auto& c : gClips) {
        uint8_t p = c.pinned; uint32_t n = (uint32_t)c.text.size();
        fwrite(&p, 1, 1, f); fwrite(&n, 4, 1, f); fwrite(c.text.data(), sizeof(wchar_t), n, f);
    }
    fclose(f);
}

static void Load() {
    FILE* f = _wfopen(DataPath().c_str(), L"rb");
    if (!f) return;
    char magic[4];
    if (fread(magic, 1, 4, f) == 4 && memcmp(magic, "CLH1", 4) == 0) {
        for (;;) {
            uint8_t p; uint32_t n;
            if (fread(&p, 1, 1, f) != 1 || fread(&n, 4, 1, f) != 1 || n > MAX_TEXT) break;
            Clip c; c.pinned = p != 0; c.text.resize(n);
            if (n && fread(&c.text[0], sizeof(wchar_t), n, f) != n) break;
            gClips.push_back(c);
        }
    }
    fclose(f);
}

// ---------- history ----------
static void Trim() {
    size_t unpinned = 0;
    for (size_t i = 0; i < gClips.size();) {
        if (!gClips[i].pinned && ++unpinned > MAX_ITEMS) gClips.erase(gClips.begin() + i); else ++i;
    }
}

static void AddClip(const std::wstring& text) {
    if (text.empty() || text.size() > MAX_TEXT) return;
    bool pinned = false;
    for (size_t i = 0; i < gClips.size(); ++i)
        if (gClips[i].text == text) { pinned = gClips[i].pinned; gClips.erase(gClips.begin() + i); break; }
    gClips.insert(gClips.begin(), Clip{ text, pinned });
    Trim(); Save();
}

static bool ReadClipboardText(std::wstring& out) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return false;
    for (int i = 0; i < 5; ++i) { if (OpenClipboard(gMain)) break; Sleep(20); if (i == 4) return false; }
    bool ok = false;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto p = (const wchar_t*)GlobalLock(h)) { out = p; ok = true; GlobalUnlock(h); }
    }
    CloseClipboard();
    return ok;
}

static bool WriteClipboardText(const std::wstring& t) {
    for (int i = 0; i < 5; ++i) { if (OpenClipboard(gMain)) break; Sleep(20); if (i == 4) return false; }
    EmptyClipboard();
    size_t bytes = (t.size() + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (g) {
        memcpy(GlobalLock(g), t.c_str(), bytes); GlobalUnlock(g);
        ok = SetClipboardData(CF_UNICODETEXT, g) != nullptr;
        if (!ok) GlobalFree(g);
    }
    CloseClipboard();
    return ok;
}

// ---------- startup ----------
static const wchar_t* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static bool StartupEnabled() {
    HKEY k; bool on = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        on = RegQueryValueExW(k, L"ClipboardHistory", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        RegCloseKey(k);
    }
    return on;
}
static void SetStartup(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring v = L"\"" + std::wstring(exe) + L"\"";
        RegSetValueExW(k, L"ClipboardHistory", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else RegDeleteValueW(k, L"ClipboardHistory");
    RegCloseKey(k);
}

// ---------- popup ----------
static std::wstring Preview(const std::wstring& t) {
    std::wstring s; s.reserve(130);
    bool space = false;
    for (wchar_t c : t) {
        if (c == L'\r' || c == L'\n' || c == L'\t' || c == L' ') { if (!space && !s.empty()) s += L' '; space = true; }
        else { s += c; space = false; }
        if (s.size() >= 120) { s += L"..."; break; }
    }
    return s;
}

static void FillList() {
    wchar_t buf[256]; GetWindowTextW(gSearch, buf, 256);
    std::wstring q = Lower(buf);
    gView.clear();
    SendMessageW(gList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(gList, LB_RESETCONTENT, 0, 0);
    for (int pass = 0; pass < 2; ++pass)                 // pinned first, then the rest
        for (size_t i = 0; i < gClips.size(); ++i) {
            if (gClips[i].pinned != (pass == 0)) continue;
            if (!q.empty() && Lower(gClips[i].text).find(q) == std::wstring::npos) continue;
            std::wstring line = (gClips[i].pinned ? L"\u2605 " : L"    ") + Preview(gClips[i].text);
            SendMessageW(gList, LB_ADDSTRING, 0, (LPARAM)line.c_str());
            gView.push_back((int)i);
        }
    if (gView.empty()) SendMessageW(gList, LB_ADDSTRING, 0, (LPARAM)(gClips.empty() ? L"    (nothing copied yet)" : L"    (no matches)"));
    else SendMessageW(gList, LB_SETCURSEL, 0, 0);
    SendMessageW(gList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(gList, nullptr, TRUE);
}

static int SelectedClip() {
    int row = (int)SendMessageW(gList, LB_GETCURSEL, 0, 0);
    return (row >= 0 && row < (int)gView.size()) ? gView[row] : -1;
}

static void ShowPopup() {
    HWND fg = GetForegroundWindow();
    if (fg != gPopup) gPrevFg = fg;
    SetWindowTextW(gSearch, L"");
    FillList();
    POINT pt; GetCursorPos(&pt);
    MONITORINFO mi{ sizeof(mi) }; GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    int w = S(520), h = S(420);
    int x = pt.x, y = pt.y;
    if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
    if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;
    SetWindowPos(gPopup, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    SetForegroundWindow(gPopup);
    SetFocus(gSearch);
}

static void HidePopup() { ShowWindow(gPopup, SW_HIDE); }

static void PasteSelected() {
    int i = SelectedClip();
    if (i < 0) return;
    std::wstring text = gClips[i].text;
    HidePopup();
    if (!WriteClipboardText(text)) return;
    if (gPrevFg && IsWindow(gPrevFg)) SetForegroundWindow(gPrevFg);
    SetTimer(gMain, TIMER_PASTE, 80, nullptr);     // give the target window a moment to get focus
}

static void SendCtrlV() {
    INPUT in[4]{};
    for (auto& i : in) i.type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_CONTROL;
    in[1].ki.wVk = 'V';
    in[2].ki.wVk = 'V';        in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].ki.wVk = VK_CONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, in, sizeof(INPUT));
}

static void TogglePin() {
    int i = SelectedClip(); if (i < 0) return;
    gClips[i].pinned = !gClips[i].pinned; Save();
    FillList();
    for (size_t r = 0; r < gView.size(); ++r)
        if (gView[r] == i) { SendMessageW(gList, LB_SETCURSEL, r, 0); break; }
}

static void RemoveSelected() {
    int i = SelectedClip(); if (i < 0) return;
    int row = (int)SendMessageW(gList, LB_GETCURSEL, 0, 0);
    gClips.erase(gClips.begin() + i); Save();
    FillList();
    int count = (int)gView.size();
    if (count) SendMessageW(gList, LB_SETCURSEL, row < count ? row : count - 1, 0);
}

static LRESULT CALLBACK SearchProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN) {
        bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
        int cur = (int)SendMessageW(gList, LB_GETCURSEL, 0, 0), n = (int)gView.size();
        switch (wp) {
        case VK_DOWN:  if (n) SendMessageW(gList, LB_SETCURSEL, cur + 1 < n ? cur + 1 : n - 1, 0); return 0;
        case VK_UP:    if (n) SendMessageW(gList, LB_SETCURSEL, cur > 0 ? cur - 1 : 0, 0); return 0;
        case VK_NEXT:  if (n) SendMessageW(gList, LB_SETCURSEL, cur + 10 < n ? cur + 10 : n - 1, 0); return 0;
        case VK_PRIOR: if (n) SendMessageW(gList, LB_SETCURSEL, cur > 10 ? cur - 10 : 0, 0); return 0;
        case VK_RETURN: PasteSelected(); return 0;
        case VK_ESCAPE: HidePopup(); return 0;
        case 'P': if (ctrl) { TogglePin(); return 0; } break;
        case VK_DELETE: if (shift) { RemoveSelected(); return 0; } break;
        }
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE || wp == 0x10 /*Ctrl+P*/)) return 0;
    return DefSubclassProc(h, msg, wp, lp);
}

static LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        auto mk = [&](const wchar_t* cls, DWORD style, DWORD ex, int id, HFONT font) {
            HWND h = CreateWindowExW(ex, cls, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)id, gInst, nullptr);
            SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE); return h; };
        gSearch = mk(L"EDIT", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_SEARCH, gFont);
        SendMessageW(gSearch, EM_SETCUEBANNER, TRUE, (LPARAM)L"Type to search your clipboard history...");
        SetWindowSubclass(gSearch, SearchProc, 1, 0);
        gList = mk(L"LISTBOX", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, WS_EX_CLIENTEDGE, IDC_LIST, gFont);
        gHint = mk(L"STATIC", SS_CENTER, 0, IDC_HINT, gSmallFont);
        SetWindowTextW(gHint, L"Enter = paste   \u00B7   Ctrl+P = pin   \u00B7   Shift+Del = remove   \u00B7   Esc = close");
        return 0;
    }
    case WM_SIZE: {
        int w = LOWORD(lp), h = HIWORD(lp), m = S(6), eh = S(26), hh = S(18);
        MoveWindow(gSearch, m, m, w - 2 * m, eh, TRUE);
        MoveWindow(gList, m, m + eh + m, w - 2 * m, h - eh - hh - 4 * m, TRUE);
        MoveWindow(gHint, m, h - hh - m, w - 2 * m, hh, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_SEARCH && HIWORD(wp) == EN_CHANGE) FillList();
        if (LOWORD(wp) == IDC_LIST && HIWORD(wp) == LBN_DBLCLK) PasteSelected();
        if (LOWORD(wp) == IDC_LIST && HIWORD(wp) == LBN_SELCHANGE) SetFocus(gSearch);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) HidePopup();
        return 0;
    case WM_CLOSE: HidePopup(); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------- tray / main ----------
static void AddTrayIcon() {
    gNid.cbSize = sizeof(gNid); gNid.hWnd = gMain; gNid.uID = 1;
    gNid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; gNid.uCallbackMessage = WM_TRAY;
    gNid.hIcon = LoadIconW(gInst, MAKEINTRESOURCEW(101)); if (!gNid.hIcon) gNid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(gNid.szTip, L"ClipboardHistory - Ctrl+Shift+V");
    Shell_NotifyIconW(NIM_ADD, &gNid);
}

static void TrayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_DEFAULT, ID_OPEN, L"Open history\tCtrl+Shift+V");
    AppendMenuW(m, MF_STRING, ID_CLEAR, L"Clear history (keeps pinned)");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (StartupEnabled() ? MF_CHECKED : 0), ID_STARTUP, L"Start with Windows");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"Exit");
    POINT p; GetCursorPos(&p);
    SetForegroundWindow(gMain);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, gMain, nullptr);
    PostMessageW(gMain, WM_NULL, 0, 0);
    DestroyMenu(m);
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TASKBARCREATED && msg != 0) { AddTrayIcon(); return 0; }
    switch (msg) {
    case WM_CLIPBOARDUPDATE: {
        std::wstring t;
        if (ReadClipboardText(t)) AddClip(t);
        return 0;
    }
    case WM_HOTKEY: if (wp == HOTKEY_ID) ShowPopup(); return 0;
    case WM_TIMER:
        if (wp == TIMER_PASTE) { KillTimer(hwnd, TIMER_PASTE); SendCtrlV(); }
        return 0;
    case WM_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP) ShowPopup();
        else if (LOWORD(lp) == WM_RBUTTONUP) TrayMenu();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_OPEN: ShowPopup(); break;
        case ID_CLEAR:
            if (MessageBoxW(nullptr, L"Delete all history items except pinned ones?", L"ClipboardHistory", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                for (size_t i = 0; i < gClips.size();) { if (!gClips[i].pinned) gClips.erase(gClips.begin() + i); else ++i; }
                Save();
            }
            break;
        case ID_STARTUP: SetStartup(!StartupEnabled()); break;
        case ID_EXIT: DestroyWindow(hwnd); break;
        }
        return 0;
    case WM_DESTROY:
        RemoveClipboardFormatListener(hwnd);
        UnregisterHotKey(hwnd, HOTKEY_ID);
        Shell_NotifyIconW(NIM_DELETE, &gNid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    gInst = hInst;
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"luarx.ClipboardHistory.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"ClipboardHistory is already running.\nLook for its icon in the system tray, or press Ctrl+Shift+V.", L"ClipboardHistory", MB_ICONINFORMATION);
        return 0;
    }
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    HDC dc = GetDC(nullptr); gDpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(nullptr, dc);
    gFont = CreateFontW(-MulDiv(10, gDpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    gSmallFont = CreateFontW(-MulDiv(8, gDpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");

    Load();

    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainProc; wc.hInstance = hInst; wc.lpszClassName = L"ClipboardHistoryMain";
    RegisterClassExW(&wc);
    gMain = CreateWindowExW(0, wc.lpszClassName, L"ClipboardHistory", 0, 0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);

    WNDCLASSEXW pc{}; pc.cbSize = sizeof(pc);
    pc.lpfnWndProc = PopupProc; pc.hInstance = hInst; pc.lpszClassName = L"ClipboardHistoryPopup";
    pc.hCursor = LoadCursor(nullptr, IDC_ARROW); pc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    pc.style = CS_DROPSHADOW;
    RegisterClassExW(&pc);
    gPopup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, pc.lpszClassName, L"Clipboard history", WS_POPUP | WS_THICKFRAME, 0, 0, S(520), S(420), nullptr, nullptr, hInst, nullptr);

    AddClipboardFormatListener(gMain);
    if (!RegisterHotKey(gMain, HOTKEY_ID, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'V'))
        MessageBoxW(nullptr, L"Ctrl+Shift+V is already used by another program.\nYou can still open the history by clicking the tray icon.", L"ClipboardHistory", MB_ICONWARNING);
    AddTrayIcon();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    if (mutex) CloseHandle(mutex);
    return 0;
}
