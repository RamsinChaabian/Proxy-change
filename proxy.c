/**
 * Proxy Switcher v2.0 - Glass Widget Edition (Light Theme)
 * Professional Real-time Proxy Monitor (IP, Port, Enable/Disable)
 * Build: gcc -O2 -s proxy.c -o ProxySwitcher.exe -lgdi32 -luser32 -lkernel32 -ladvapi32 -lwininet -lshell32 -ldwmapi -lcomctl32 -lmsimg32 -mwindows
 */

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <windowsx.h>
#include <winreg.h>
#include <wininet.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== Constants ====================
#define APP_NAME            L"ProxySwitcher"
#define APP_TITLE           L"Proxy Switcher"
#define WINDOW_WIDTH        290
#define WINDOW_HEIGHT       200
#define CORNER_RADIUS       16
#define WM_TRAYICON         (WM_APP + 1)
#define WM_USER_UPDATE_PROXY     (WM_APP + 2)
#define WM_USER_UPDATE_PROXYFULL (WM_APP + 3)
#define ID_TRAY_EXIT        1001
#define ID_TRAY_SHOW        1002
#define ID_TRAY_TOGGLE      1003

#define BTN_APPLY           2001
#define BTN_TOGGLE          2002
#define BTN_TRAY            2003
#define BTN_CLOSE           2004

// ==================== Light Theme Colors ====================
#define COLOR_BG_PRIMARY        RGB(255, 255, 255)
#define COLOR_BG_SECONDARY      RGB(245, 245, 250)
#define COLOR_BG_INPUT          RGB(235, 235, 240)
#define COLOR_TEXT_PRIMARY      RGB(30, 30, 50)
#define COLOR_TEXT_SECONDARY    RGB(120, 120, 140)
#define COLOR_ACCENT            RGB(0, 120, 255)
#define COLOR_ACCENT_HOVER      RGB(0, 90, 210)
#define COLOR_SUCCESS           RGB(0, 200, 83)
#define COLOR_DANGER            RGB(220, 50, 50)
#define COLOR_BORDER            RGB(220, 220, 230)

// ==================== Global Variables ====================
HINSTANCE       g_hInst             = NULL;
HWND            g_hWnd              = NULL;
NOTIFYICONDATAW g_nid               = {0};
BOOL            g_proxyEnabled      = FALSE;
HWND            g_hIPInput          = NULL;
HWND            g_hPortInput        = NULL;
HWND            g_hToggleBtn        = NULL;
HWND            g_hApplyBtn         = NULL;
HWND            g_hTrayBtn          = NULL;
HWND            g_hCloseBtn         = NULL;
HWND            g_hStatusIndicator  = NULL;
HWND            g_hTitleLabel       = NULL;
HFONT           g_hFontUI           = NULL;
HFONT           g_hFontTitle        = NULL;
HFONT           g_hFontMono         = NULL;
POINT           g_lastMouse         = {0};
BOOL            g_dragging          = FALSE;
WNDPROC         g_oldIPProc         = NULL;
WNDPROC         g_oldPortProc       = NULL;
HMENU           g_hTrayMenu         = NULL;
HANDLE          g_hMutex            = NULL;
HANDLE          g_hMonitorThread    = NULL;
BOOL            g_bMonitorRunning   = TRUE;

// ==================== Forward Declarations ====================
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK InputSubclassProc(HWND, UINT, WPARAM, LPARAM);
BOOL InitApplication(HINSTANCE);
HWND CreateMainWindow(HINSTANCE, int);
void CreateControls(HWND);
void UpdateProxyStatusUI(void);
BOOL IsProxyEnabled(void);
BOOL SetProxyState(BOOL enable);
BOOL SetProxyServer(const wchar_t* ip, const wchar_t* port);
void GetCurrentProxy(wchar_t* ip, size_t ipSize, wchar_t* port, size_t portSize);
void AddTrayIcon(HWND);
void RemoveTrayIcon(void);
void ShowTrayMenu(HWND);
void UpdateTrayIcon(BOOL enabled);
HICON CreateNeonIcon(BOOL active);
void PaintWindow(HWND hWnd, HDC hdc);
void DrawRoundedButton(HDC hdc, RECT* rc, COLORREF bg, BOOL hover, BOOL pressed);
void DrawToggleSwitch(HDC hdc, int x, int y, BOOL enabled, BOOL hover);
COLORREF LightenColor(COLORREF color, int amount);
DWORD WINAPI MonitorProxyChanges(LPVOID lpParam);

// ==================== WinMain ====================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    g_hMutex = CreateMutexW(NULL, TRUE, L"Global\\ProxySwitcher_Unique_Mutex");
    if (g_hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND hWndExisting = FindWindowW(APP_NAME, NULL);
        if (hWndExisting)
        {
            if (!IsWindowVisible(hWndExisting))
                ShowWindow(hWndExisting, SW_SHOW);
            SetForegroundWindow(hWndExisting);
        }
        return 0;
    }
    
    g_hInst = hInstance;
    
    INITCOMMONCONTROLSEX icc = {sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    
    if (!InitApplication(hInstance))
        return 0;
    
    g_hWnd = CreateMainWindow(hInstance, nCmdShow);
    if (!g_hWnd)
        return 0;
    
    g_proxyEnabled = IsProxyEnabled();
    UpdateProxyStatusUI();
    AddTrayIcon(g_hWnd);
    UpdateTrayIcon(g_proxyEnabled);
    
    g_hMonitorThread = CreateThread(NULL, 0, MonitorProxyChanges, NULL, 0, NULL);
    
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    g_bMonitorRunning = FALSE;
    if (g_hMonitorThread)
        WaitForSingleObject(g_hMonitorThread, 1000);
    
    RemoveTrayIcon();
    
    if (g_hMutex) CloseHandle(g_hMutex);
    
    return (int)msg.wParam;
}

// ==================== Monitor Thread (با نظارت کامل بر IP, Port, Status) ====================
DWORD WINAPI MonitorProxyChanges(LPVOID lpParam)
{
    HKEY hKey;
    DWORD dwFilter = REG_NOTIFY_CHANGE_LAST_SET;
    
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
        0, KEY_NOTIFY, &hKey) == ERROR_SUCCESS)
    {
        while (g_bMonitorRunning)
        {
            if (RegNotifyChangeKeyValue(hKey, TRUE, dwFilter, NULL, FALSE) == ERROR_SUCCESS)
            {
                BOOL newState = IsProxyEnabled();
                
                wchar_t ip[256] = {0}, port[64] = {0};
                GetCurrentProxy(ip, 256, port, 64);
                
                wchar_t* fullData = (wchar_t*)malloc(512 * sizeof(wchar_t));
                if (fullData)
                {
                    wsprintfW(fullData, L"%s\n%s", ip, port);
                    PostMessage(g_hWnd, WM_USER_UPDATE_PROXYFULL, newState, (LPARAM)fullData);
                }
            }
        }
        RegCloseKey(hKey);
    }
    return 0;
}

// ==================== Window Registration ====================
BOOL InitApplication(HINSTANCE hInstance)
{
    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(NULL, IDC_HAND);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = APP_NAME;
    
    wc.hIcon   = CreateNeonIcon(TRUE);
    wc.hIconSm = CreateNeonIcon(TRUE);
    
    return RegisterClassExW(&wc) != 0;
}

// ==================== Main Window Creation ====================
HWND CreateMainWindow(HINSTANCE hInstance, int nCmdShow)
{
    int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenWidth - WINDOW_WIDTH) / 2;
    int y = (screenHeight - WINDOW_HEIGHT) / 2;
    
    HWND hWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        APP_NAME, APP_TITLE, WS_POPUP,
        x, y, WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, hInstance, NULL
    );
    
    if (!hWnd) return NULL;
    
    SetLayeredWindowAttributes(hWnd, 0, 245, LWA_ALPHA);
    
    DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));
    
    CreateControls(hWnd);
    
    wchar_t ip[256] = {0}, port[64] = {0};
    GetCurrentProxy(ip, 256, port, 64);
    if (ip[0]) SetWindowTextW(g_hIPInput, ip);
    if (port[0]) SetWindowTextW(g_hPortInput, port);
    
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    
    return hWnd;
}

// ==================== Create Controls ====================
void CreateControls(HWND hParent)
{
    HINSTANCE hInst = g_hInst;
    
    g_hFontUI    = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_hFontTitle = CreateFontW(18, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_hFontMono  = CreateFontW(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
    
    g_hTitleLabel = CreateWindowExW(0, L"STATIC", L"Proxy Switcher",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        10, 8, 270, 22, hParent, NULL, hInst, NULL);
    SendMessageW(g_hTitleLabel, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
    
    g_hIPInput = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
        12, 38, 180, 30, hParent, NULL, hInst, NULL);
    SendMessageW(g_hIPInput, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);
    SendMessageW(g_hIPInput, EM_SETCUEBANNER, FALSE, (LPARAM)L"IP Address (127.0.0.1)");
    g_oldIPProc = (WNDPROC)SetWindowLongPtrW(g_hIPInput, GWLP_WNDPROC, (LONG_PTR)InputSubclassProc);
    
    g_hPortInput = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_NUMBER,
        200, 38, 78, 30, hParent, NULL, hInst, NULL);
    SendMessageW(g_hPortInput, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);
    SendMessageW(g_hPortInput, EM_SETCUEBANNER, FALSE, (LPARAM)L"Port");
    g_oldPortProc = (WNDPROC)SetWindowLongPtrW(g_hPortInput, GWLP_WNDPROC, (LONG_PTR)InputSubclassProc);
    
    g_hApplyBtn = CreateWindowExW(0, L"BUTTON", L"Apply",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        12, 78, 266, 34, hParent, (HMENU)BTN_APPLY, hInst, NULL);
    
    g_hToggleBtn = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        12, 122, 210, 30, hParent, (HMENU)BTN_TOGGLE, hInst, NULL);
    
    g_hStatusIndicator = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        228, 124, 50, 26, hParent, NULL, hInst, NULL);
    SendMessageW(g_hStatusIndicator, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
    
    g_hTrayBtn = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        12, 162, 130, 28, hParent, (HMENU)BTN_TRAY, hInst, NULL);
    
    g_hCloseBtn = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        152, 162, 126, 28, hParent, (HMENU)BTN_CLOSE, hInst, NULL);
}

// ==================== Input Subclass Proc ====================
LRESULT CALLBACK InputSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WNDPROC oldProc = (hWnd == g_hIPInput) ? g_oldIPProc : g_oldPortProc;
    
    switch (msg)
    {
        case WM_PAINT:
        {
            CallWindowProcW(oldProc, hWnd, msg, wParam, lParam);
            
            HDC hdc = GetWindowDC(hWnd);
            RECT rc;
            GetClientRect(hWnd, &rc);
            
            HPEN pen = CreatePen(PS_SOLID, 1, COLOR_ACCENT);
            SelectObject(hdc, pen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, 0, 0, rc.right, rc.bottom, 6, 6);
            
            DeleteObject(pen);
            ReleaseDC(hWnd, hdc);
            return 0;
        }
    }
    return CallWindowProcW(oldProc, hWnd, msg, wParam, lParam);
}

// ==================== Window Procedure ====================
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_CREATE:
            return 0;
        
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            PaintWindow(hWnd, hdc);
            EndPaint(hWnd, &ps);
            return 0;
        }
        
        case WM_ERASEBKGND:
            return 1;
        
        case WM_CTLCOLORSTATIC:
        {
            HDC hdc = (HDC)wParam;
            HWND ctrl = (HWND)lParam;
            SetBkMode(hdc, TRANSPARENT);
            
            if (ctrl == g_hTitleLabel)
                SetTextColor(hdc, COLOR_ACCENT);
            else if (ctrl == g_hStatusIndicator)
                SetTextColor(hdc, g_proxyEnabled ? COLOR_SUCCESS : COLOR_TEXT_SECONDARY);
            else
                SetTextColor(hdc, COLOR_TEXT_PRIMARY);
            
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        
        case WM_CTLCOLOREDIT:
        {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, COLOR_BG_INPUT);
            SetTextColor(hdc, COLOR_TEXT_PRIMARY);
            static HBRUSH inputBrush = NULL;
            if (inputBrush) DeleteObject(inputBrush);
            inputBrush = CreateSolidBrush(COLOR_BG_INPUT);
            return (LRESULT)inputBrush;
        }
        
        case WM_USER_UPDATE_PROXYFULL:
        {
            wchar_t* fullData = (wchar_t*)lParam;
            BOOL newState = (BOOL)wParam;
            
            if (fullData)
            {
                wchar_t ip[256] = {0}, port[64] = {0};
                wchar_t* newline = wcschr(fullData, L'\n');
                if (newline)
                {
                    wcsncpy_s(ip, 256, fullData, newline - fullData);
                    wcscpy_s(port, 64, newline + 1);
                    
                    SetWindowTextW(g_hIPInput, ip);
                    SetWindowTextW(g_hPortInput, port);
                }
                free(fullData);
            }
            
            if (newState != g_proxyEnabled)
            {
                g_proxyEnabled = newState;
                UpdateProxyStatusUI();
                UpdateTrayIcon(g_proxyEnabled);
            }
            return 0;
        }
        
        case WM_LBUTTONDOWN:
        {
            g_dragging = TRUE;
            g_lastMouse.x = GET_X_LPARAM(lParam);
            g_lastMouse.y = GET_Y_LPARAM(lParam);
            SetCapture(hWnd);
            return 0;
        }
        
        case WM_MOUSEMOVE:
        {
            if (g_dragging)
            {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                int dx = x - g_lastMouse.x;
                int dy = y - g_lastMouse.y;
                
                RECT rc;
                GetWindowRect(hWnd, &rc);
                SetWindowPos(hWnd, NULL, rc.left + dx, rc.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            return 0;
        }
        
        case WM_LBUTTONUP:
        {
            g_dragging = FALSE;
            ReleaseCapture();
            return 0;
        }
        
        case WM_DRAWITEM:
        {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;
            UINT id = dis->CtlID;
            BOOL hover = (dis->itemState & ODS_HOTLIGHT) != 0;
            BOOL pressed = (dis->itemState & ODS_SELECTED) != 0;
            
            switch (id)
            {
                case BTN_APPLY:
                {
                    SetBkMode(hdc, TRANSPARENT);
                    COLORREF bg = pressed ? COLOR_ACCENT_HOVER : 
                                 hover ? LightenColor(COLOR_ACCENT, 20) : COLOR_ACCENT;
                    DrawRoundedButton(hdc, &rc, bg, hover, pressed);
                    
                    HFONT oldFont = SelectObject(hdc, g_hFontUI);
                    SetTextColor(hdc, RGB(255, 255, 255));
                    DrawTextW(hdc, L"⚡ Apply Settings", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, oldFont);
                    break;
                }
                
                case BTN_TOGGLE:
                {
                    SetBkMode(hdc, TRANSPARENT);
                    int yCenter = rc.top + (rc.bottom - rc.top) / 2;
                    DrawToggleSwitch(hdc, 8, yCenter - 12, g_proxyEnabled, hover);
                    
                    HFONT oldFont = SelectObject(hdc, g_hFontUI);
                    SetTextColor(hdc, COLOR_TEXT_PRIMARY);
                    RECT textRc = {60, rc.top, rc.right - 10, rc.bottom};
                    DrawTextW(hdc, g_proxyEnabled ? L"Connected" : L"Disconnected", -1, &textRc, 
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, oldFont);
                    break;
                }
                
                case BTN_TRAY:
                {
                    SetBkMode(hdc, TRANSPARENT);
                    COLORREF bg = pressed ? COLOR_BG_INPUT : 
                                 hover ? LightenColor(COLOR_BG_SECONDARY, 10) : COLOR_BG_SECONDARY;
                    DrawRoundedButton(hdc, &rc, bg, hover, pressed);
                    
                    HFONT oldFont = SelectObject(hdc, g_hFontUI);
                    SetTextColor(hdc, COLOR_TEXT_SECONDARY);
                    DrawTextW(hdc, L"━ To Tray", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, oldFont);
                    break;
                }
                
                case BTN_CLOSE:
                {
                    SetBkMode(hdc, TRANSPARENT);
                    COLORREF bg = pressed ? RGB(180, 30, 30) : 
                                 hover ? RGB(255, 70, 70) : COLOR_DANGER;
                    DrawRoundedButton(hdc, &rc, bg, hover, pressed);
                    
                    HFONT oldFont = SelectObject(hdc, g_hFontUI);
                    SetTextColor(hdc, RGB(255, 255, 255));
                    DrawTextW(hdc, L"✕ Exit", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, oldFont);
                    break;
                }
            }
            return TRUE;
        }
        
        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
                case BTN_APPLY:
                {
                    wchar_t ip[256] = {0}, port[64] = {0};
                    GetWindowTextW(g_hIPInput, ip, 256);
                    GetWindowTextW(g_hPortInput, port, 64);
                    
                    if (wcslen(ip) == 0 || wcslen(port) == 0)
                    {
                        MessageBoxW(hWnd, L"Please enter both IP and Port", L"Proxy Switcher", MB_ICONINFORMATION);
                        break;
                    }
                    
                    if (SetProxyServer(ip, port))
                    {
                        SetProxyState(TRUE);
                        g_proxyEnabled = TRUE;
                        UpdateProxyStatusUI();
                        UpdateTrayIcon(g_proxyEnabled);
                    }
                    else
                    {
                        MessageBoxW(hWnd, L"Failed to apply proxy settings", L"Error", MB_ICONERROR);
                    }
                    break;
                }
                
                case BTN_TOGGLE:
                {
                    g_proxyEnabled = !g_proxyEnabled;
                    SetProxyState(g_proxyEnabled);
                    UpdateProxyStatusUI();
                    UpdateTrayIcon(g_proxyEnabled);
                    break;
                }
                
                case BTN_TRAY:
                    ShowWindow(hWnd, SW_HIDE);
                    break;
                
                case BTN_CLOSE:
                    DestroyWindow(hWnd);
                    break;
            }
            return 0;
        }
        
        case WM_TRAYICON:
        {
            if (lParam == WM_LBUTTONDOWN)
            {
                if (IsWindowVisible(g_hWnd))
                    ShowWindow(g_hWnd, SW_HIDE);
                else
                {
                    ShowWindow(g_hWnd, SW_SHOW);
                    SetForegroundWindow(g_hWnd);
                }
            }
            else if (lParam == WM_RBUTTONUP)
            {
                ShowTrayMenu(g_hWnd);
            }
            return 0;
        }
        
        case WM_DESTROY:
        {
            RemoveTrayIcon();
            if (g_hFontUI) DeleteObject(g_hFontUI);
            if (g_hFontTitle) DeleteObject(g_hFontTitle);
            if (g_hFontMono) DeleteObject(g_hFontMono);
            if (g_hTrayMenu) DestroyMenu(g_hTrayMenu);
            PostQuitMessage(0);
            return 0;
        }
    }
    
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

// ==================== Painting Functions ====================
void PaintWindow(HWND hWnd, HDC hdc)
{
    RECT rc;
    GetClientRect(hWnd, &rc);
    
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP oldBitmap = SelectObject(memDC, memBitmap);
    
    for (int i = 0; i < rc.bottom; i++)
    {
        int whiteness = 245 + (int)(10.0 * i / rc.bottom);
        COLORREF color = RGB(whiteness, whiteness, whiteness + 5);
        
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        SelectObject(memDC, pen);
        MoveToEx(memDC, 0, i, NULL);
        LineTo(memDC, rc.right, i);
        DeleteObject(pen);
    }
    
    HPEN borderPen = CreatePen(PS_SOLID, 1, COLOR_BORDER);
    HBRUSH bgBrush = CreateSolidBrush(COLOR_BG_PRIMARY);
    SelectObject(memDC, bgBrush);
    SelectObject(memDC, borderPen);
    RoundRect(memDC, 0, 0, rc.right, rc.bottom, CORNER_RADIUS, CORNER_RADIUS);
    
    HPEN glowPen = CreatePen(PS_SOLID, 2, COLOR_ACCENT);
    SelectObject(memDC, glowPen);
    SelectObject(memDC, GetStockObject(NULL_BRUSH));
    RoundRect(memDC, 1, 1, rc.right - 1, rc.bottom - 1, CORNER_RADIUS - 1, CORNER_RADIUS - 1);
    
    HPEN softGlowPen = CreatePen(PS_SOLID, 1, RGB(180, 210, 255));
    SelectObject(memDC, softGlowPen);
    RoundRect(memDC, 2, 2, rc.right - 2, rc.bottom - 2, CORNER_RADIUS - 2, CORNER_RADIUS - 2);
    
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
    
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    DeleteObject(bgBrush);
    DeleteObject(borderPen);
    DeleteObject(glowPen);
    DeleteObject(softGlowPen);
}

void DrawRoundedButton(HDC hdc, RECT* rc, COLORREF bg, BOOL hover, BOOL pressed)
{
    int radius = 8;
    
    HBRUSH brush = CreateSolidBrush(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, bg);
    SelectObject(hdc, brush);
    SelectObject(hdc, pen);
    
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);
    
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawToggleSwitch(HDC hdc, int x, int y, BOOL enabled, BOOL hover)
{
    int width = 44, height = 24;
    int knobSize = 18;
    int knobX = enabled ? x + width - knobSize - 3 : x + 3;
    int knobY = y + (height - knobSize) / 2;
    
    COLORREF trackColor = enabled ? COLOR_SUCCESS : RGB(200, 200, 210);
    HBRUSH trackBrush = CreateSolidBrush(trackColor);
    HPEN trackPen = CreatePen(PS_SOLID, 1, trackColor);
    SelectObject(hdc, trackBrush);
    SelectObject(hdc, trackPen);
    RoundRect(hdc, x, y, x + width, y + height, 12, 12);
    
    HBRUSH shadowBrush = CreateSolidBrush(RGB(200, 200, 210));
    SelectObject(hdc, shadowBrush);
    Ellipse(hdc, knobX + 1, knobY + 1, knobX + knobSize + 1, knobY + knobSize + 1);
    
    COLORREF knobColor = RGB(255, 255, 255);
    HBRUSH knobBrush = CreateSolidBrush(knobColor);
    HPEN knobPen = CreatePen(PS_SOLID, 1, RGB(210, 210, 220));
    SelectObject(hdc, knobBrush);
    SelectObject(hdc, knobPen);
    Ellipse(hdc, knobX, knobY, knobX + knobSize, knobY + knobSize);
    
    DeleteObject(trackBrush);
    DeleteObject(trackPen);
    DeleteObject(shadowBrush);
    DeleteObject(knobBrush);
    DeleteObject(knobPen);
}

// ==================== Helper Functions ====================
COLORREF LightenColor(COLORREF color, int amount)
{
    int r = min(GetRValue(color) + amount, 255);
    int g = min(GetGValue(color) + amount, 255);
    int b = min(GetBValue(color) + amount, 255);
    return RGB(r, g, b);
}

// ==================== Proxy Functions ====================
BOOL IsProxyEnabled(void)
{
    HKEY hKey;
    DWORD enabled = 0, size = sizeof(DWORD);
    
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
        0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        RegQueryValueExW(hKey, L"ProxyEnable", NULL, NULL, (LPBYTE)&enabled, &size);
        RegCloseKey(hKey);
    }
    return enabled != 0;
}

BOOL SetProxyState(BOOL enable)
{
    HKEY hKey;
    DWORD value = enable ? 1 : 0;
    BOOL result = FALSE;
    
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
        0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
    {
        result = RegSetValueExW(hKey, L"ProxyEnable", 0, REG_DWORD, (LPBYTE)&value, sizeof(DWORD)) == ERROR_SUCCESS;
        RegCloseKey(hKey);
    }
    
    if (result)
    {
        InternetSetOptionW(NULL, INTERNET_OPTION_SETTINGS_CHANGED, NULL, 0);
        InternetSetOptionW(NULL, INTERNET_OPTION_REFRESH, NULL, 0);
    }
    return result;
}

BOOL SetProxyServer(const wchar_t* ip, const wchar_t* port)
{
    HKEY hKey;
    BOOL result = FALSE;
    wchar_t server[512];
    wsprintfW(server, L"%s:%s", ip, port);
    
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
        0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
    {
        result = RegSetValueExW(hKey, L"ProxyServer", 0, REG_SZ,
            (LPBYTE)server, (DWORD)(wcslen(server) + 1) * sizeof(wchar_t)) == ERROR_SUCCESS;
        RegCloseKey(hKey);
    }
    return result;
}

void GetCurrentProxy(wchar_t* ip, size_t ipSize, wchar_t* port, size_t portSize)
{
    HKEY hKey;
    wchar_t server[512] = {0};
    DWORD size = sizeof(server);
    
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
        0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW(hKey, L"ProxyServer", NULL, NULL, (LPBYTE)server, &size) == ERROR_SUCCESS)
        {
            wchar_t* colon = wcschr(server, L':');
            if (colon)
            {
                *colon = 0;
                wcscpy_s(ip, ipSize, server);
                wcscpy_s(port, portSize, colon + 1);
            }
        }
        RegCloseKey(hKey);
    }
}

// ==================== System Tray ====================
void AddTrayIcon(HWND hWnd)
{
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = CreateNeonIcon(g_proxyEnabled);
    wcscpy_s(g_nid.szTip, 128, L"Proxy Switcher");
    
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void RemoveTrayIcon(void)
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

void UpdateTrayIcon(BOOL enabled)
{
    if (g_nid.hWnd)
    {
        if (g_nid.hIcon)
            DestroyIcon(g_nid.hIcon);
        g_nid.hIcon = CreateNeonIcon(enabled);
        g_nid.uFlags = NIF_ICON;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    }
}

void ShowTrayMenu(HWND hWnd)
{
    if (g_hTrayMenu)
        DestroyMenu(g_hTrayMenu);
    
    g_hTrayMenu = CreatePopupMenu();
    
    AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_SHOW, L"📱 Show Window");
    AppendMenuW(g_hTrayMenu, MF_SEPARATOR, 0, NULL);
    
    if (g_proxyEnabled)
        AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_TOGGLE, L"🟢 Disable Proxy");
    else
        AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_TOGGLE, L"🔴 Enable Proxy");
    
    AppendMenuW(g_hTrayMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_EXIT, L"❌ Exit");
    
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hWnd);
    
    int cmd = TrackPopupMenu(g_hTrayMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
    
    switch (cmd)
    {
        case ID_TRAY_SHOW:
            ShowWindow(hWnd, SW_SHOW);
            SetForegroundWindow(hWnd);
            break;
        case ID_TRAY_TOGGLE:
            g_proxyEnabled = !g_proxyEnabled;
            SetProxyState(g_proxyEnabled);
            UpdateProxyStatusUI();
            UpdateTrayIcon(g_proxyEnabled);
            break;
        case ID_TRAY_EXIT:
            DestroyWindow(hWnd);
            break;
    }
}

// ==================== UI Helpers ====================
void UpdateProxyStatusUI(void)
{
    if (g_hStatusIndicator)
    {
        SetWindowTextW(g_hStatusIndicator, g_proxyEnabled ? L"● ON" : L"○ OFF");
        InvalidateRect(g_hStatusIndicator, NULL, TRUE);
    }
    
    if (g_hToggleBtn)
        InvalidateRect(g_hToggleBtn, NULL, TRUE);
    
    if (g_hWnd)
        InvalidateRect(g_hWnd, NULL, TRUE);
}

HICON CreateNeonIcon(BOOL active)
{
    HDC hdc = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, 32, 32);
    HBITMAP hOldBitmap = SelectObject(memDC, hBitmap);
    
    RECT rcFull = {0, 0, 32, 32};
    HBRUSH bgBrush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(memDC, &rcFull, bgBrush);
    DeleteObject(bgBrush);
    
    COLORREF mainColor, darkColor, glowColor;
    if (active)
    {
        mainColor = RGB(0, 200, 83);
        darkColor = RGB(0, 150, 60);
        glowColor = RGB(100, 255, 150);
    }
    else
    {
        mainColor = RGB(180, 180, 190);
        darkColor = RGB(150, 150, 160);
        glowColor = RGB(210, 210, 220);
    }
    
    for (int r = 14; r <= 18; r++)
    {
        HBRUSH glowBrush = CreateSolidBrush(glowColor);
        HPEN glowPen = CreatePen(PS_SOLID, 1, glowColor);
        SelectObject(memDC, glowBrush);
        SelectObject(memDC, glowPen);
        Ellipse(memDC, 16 - r, 16 - r, 16 + r, 16 + r);
        DeleteObject(glowBrush);
        DeleteObject(glowPen);
    }
    
    HBRUSH hBrush = CreateSolidBrush(mainColor);
    HPEN hPen = CreatePen(PS_SOLID, 2, darkColor);
    
    SelectObject(memDC, hBrush);
    SelectObject(memDC, hPen);
    Ellipse(memDC, 4, 4, 28, 28);
    
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 255));
    HFONT hFont = CreateFontW(18, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    SelectObject(memDC, hFont);
    
    RECT rcText = {0, 0, 32, 32};
    if (active)
        DrawTextW(memDC, L"✓", 1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    else
        DrawTextW(memDC, L"●", 1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    
    SelectObject(memDC, hOldBitmap);
    DeleteObject(hBrush);
    DeleteObject(hPen);
    DeleteObject(hFont);
    DeleteDC(memDC);
    ReleaseDC(NULL, hdc);
    
    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmColor = hBitmap;
    ii.hbmMask = hBitmap;
    HICON hIcon = CreateIconIndirect(&ii);
    DeleteObject(hBitmap);
    
    return hIcon;
}