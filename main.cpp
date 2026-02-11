#define _WIN32_WINNT 0x0600
#define OEMRESOURCE
#include <windows.h>
#include <psapi.h>
#include <mmsystem.h>
#include <cmath>
#include <string>
#include <algorithm>

#pragma comment(lib, "winmm.lib")

// --- RESOURCE IDs (Matches resources.rc) ---
#define ID_CUR_MIDDLE "ID_CUR_MIDDLE"
#define ID_CUR_NORTH  "ID_CUR_NORTH"
#define ID_CUR_SOUTH  "ID_CUR_SOUTH"

#ifndef OCR_NORMAL
#define OCR_NORMAL 32512
#endif
#ifndef OCR_IBEAM
#define OCR_IBEAM  32513
#endif
#ifndef OCR_HAND
#define OCR_HAND   32649
#endif

// --- CONFIGURATION ---
const int    SCROLL_TICK_MS    = 2;

const int    DEADZONE          = 15;
const float  SPEED_LINEAR      = 10.0f;
const float  SPEED_QUADRATIC   = 0.10f;
const float  MAX_SPEED         = 15000.0f;

// --- STATE ---
HHOOK  hMouseHook    = NULL;
HHOOK  hKeyboardHook = NULL;
HANDLE hScrollThread = NULL;
HWND   targetWindow  = NULL;
HWND   hMsgWindow    = NULL;
POINT  anchorPoint;
int    currentCursorDir = -99;
UINT   WM_TGMIDSCROLL_QUIT = 0;

volatile bool isAutoScrolling = false;
volatile bool isEnabled       = true;
volatile bool threadRunning   = false;
volatile bool dragDetected    = false;

// --- HELPER: REGISTER AUTO-START ---
void RegisterAutoStart() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        RegSetValueExA(hKey, "TgMidScroll", 0, REG_SZ,
                       (const BYTE*)exePath, (DWORD)(strlen(exePath) + 1));
        RegCloseKey(hKey);
    }
}

// --- HELPER: RESTORE CURSOR ---
void RestoreSystemCursor() {
    SystemParametersInfo(SPI_SETCURSORS, 0, NULL, 0);
    currentCursorDir = -99;
}

// --- HELPER: CHANGE CURSOR ICON ---
void UpdateCursorShape(int dir) {
    if (dir == currentCursorDir) return;

    HCURSOR hNewCursor = NULL;
    HINSTANCE hInst = GetModuleHandle(NULL);

    if (dir == 0)       hNewCursor = LoadCursor(hInst, ID_CUR_MIDDLE);
    else if (dir == 1)  hNewCursor = LoadCursor(hInst, ID_CUR_NORTH);
    else if (dir == -1) hNewCursor = LoadCursor(hInst, ID_CUR_SOUTH);

    if (!hNewCursor) hNewCursor = LoadCursor(NULL, IDC_SIZENS);

    if (hNewCursor) {
        SetSystemCursor(CopyCursor(hNewCursor), OCR_NORMAL);
        SetSystemCursor(CopyCursor(hNewCursor), OCR_IBEAM);
        SetSystemCursor(CopyCursor(hNewCursor), OCR_HAND);
        currentCursorDir = dir;
    }
}

// --- HELPER: CHECK IF HWND BELONGS TO telegram.exe ---
bool IsTelegramProcess(HWND hwnd) {
    if (!hwnd) return false;
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) return false;
    bool result = false;
    char buffer[MAX_PATH];
    if (GetModuleFileNameExA(hProcess, NULL, buffer, MAX_PATH)) {
        std::string path = buffer;
        std::transform(path.begin(), path.end(), path.begin(), ::tolower);
        result = (path.find("telegram.exe") != std::string::npos);
    }
    CloseHandle(hProcess);
    return result;
}

// --- HELPER: CHECK IF WINDOW IS MEDIA VIEWER ---
bool IsMediaViewerWindow(HWND hwnd) {
    if (!hwnd) return false;
    HWND hRoot = GetAncestor(hwnd, GA_ROOT);
    if (!hRoot) hRoot = hwnd;
    char title[256];
    if (GetWindowTextA(hRoot, title, sizeof(title)) > 0) {
        std::string sTitle = title;
        std::transform(sTitle.begin(), sTitle.end(), sTitle.begin(), ::tolower);
        if (sTitle.find("media viewer") != std::string::npos)
            return true;
    }
    return false;
}

// --- HELPER: CHECK IF WINDOW IS SCROLLABLE TELEGRAM ---
bool IsScrollableTelegramWindow(HWND hwnd) {
    if (!hwnd) return false;

    HWND hRoot = GetAncestor(hwnd, GA_ROOT);
    if (!hRoot) hRoot = hwnd;

    if (!IsTelegramProcess(hRoot)) return false;

    LONG_PTR style = GetWindowLongPtr(hRoot, GWL_STYLE);
    if (!(style & WS_THICKFRAME)) return false;

    if (IsMediaViewerWindow(hRoot)) return false;

    return true;
}

// --- THREAD: SCROLLING ENGINE ---
DWORD WINAPI AutoScrollThread(LPVOID) {
    threadRunning = true;
    timeBeginPeriod(1);
    UpdateCursorShape(0);

    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    float accumulator = 0.0f;

    HWND targetRoot = GetAncestor(targetWindow, GA_ROOT);
    if (!targetRoot) targetRoot = targetWindow;

    while (isAutoScrolling && isEnabled) {
        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - prev.QuadPart) / (float)freq.QuadPart;
        prev = now;
        if (dt > 0.1f) dt = 0.1f;

        if (!targetWindow || !IsWindow(targetWindow)) {
            isAutoScrolling = false;
            break;
        }

        HWND fg = GetForegroundWindow();
        if (fg != NULL && fg != targetRoot) {
            isAutoScrolling = false;
            break;
        }

        POINT cur;
        GetCursorPos(&cur);
        int deltaY   = anchorPoint.y - cur.y;
        int distance = std::abs(deltaY);

        if (distance > DEADZONE) {
            int   sign      = (deltaY > 0) ? 1 : -1;
            float effective = (float)(distance - DEADZONE);
            dragDetected = true;

            float speed = effective * SPEED_LINEAR
                        + effective * effective * SPEED_QUADRATIC;
            if (speed > MAX_SPEED) speed = MAX_SPEED;

            accumulator += sign * speed * dt;

            UpdateCursorShape(sign);

            int wd = (int)accumulator;
            if (wd != 0) {
                if (wd >  32767) wd =  32767;
                if (wd < -32768) wd = -32768;

                PostMessage(targetWindow, WM_MOUSEWHEEL,
                            MAKEWPARAM(0, (short)wd),
                            MAKELPARAM(anchorPoint.x, anchorPoint.y));
                accumulator -= (float)wd;
            }
        } else {
            UpdateCursorShape(0);
            accumulator = 0.0f;
        }

        Sleep(SCROLL_TICK_MS);
    }

    timeEndPeriod(1);
    RestoreSystemCursor();
    threadRunning = false;
    return 0;
}

// --- HOOK: MOUSE ---
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && isEnabled) {
        MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lParam;

        if (wParam == WM_MBUTTONDOWN) {
            HWND hwndUnder = WindowFromPoint(ms->pt);
            if (IsScrollableTelegramWindow(hwndUnder)) {
                if (isAutoScrolling) {
                    isAutoScrolling = false;
                } else if (!threadRunning) {
                    if (hScrollThread) {
                        CloseHandle(hScrollThread);
                        hScrollThread = NULL;
                    }
                    anchorPoint    = ms->pt;
                    targetWindow   = GetAncestor(hwndUnder, GA_ROOT);
                    if (!targetWindow) targetWindow = hwndUnder;
                    dragDetected    = false;
                    isAutoScrolling = true;
                    hScrollThread   = CreateThread(NULL, 0, AutoScrollThread, NULL, 0, NULL);
                }
                return 1;
            }
        }
        else if (wParam == WM_MBUTTONUP) {
            if (isAutoScrolling) {
                if (dragDetected)
                    isAutoScrolling = false;
                return 1;
            }
        }
        else if (wParam == WM_MOUSEWHEEL) {
            if (isAutoScrolling)
                return 1;
        }
        else if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN) {
            if (isAutoScrolling)
                isAutoScrolling = false;
        }
    }
    return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
}

// --- HOOK: KEYBOARD ---
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;

        if (p->vkCode == VK_ESCAPE && isAutoScrolling) {
            isAutoScrolling = false;
            return 1;
        }

        bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

        // Ctrl+F11  — quit
        if (ctrlDown && p->vkCode == VK_F11) {
            if (IsTelegramProcess(GetForegroundWindow())) {
                isAutoScrolling = false;
                RestoreSystemCursor();
                PostQuitMessage(0);
                return 1;
            }
        }
        // Ctrl+F12  — toggle enable / disable
        if (ctrlDown && p->vkCode == VK_F12) {
            if (IsTelegramProcess(GetForegroundWindow())) {
                isEnabled = !isEnabled;
                if (!isEnabled && isAutoScrolling)
                    isAutoScrolling = false;
                Beep(isEnabled ? 1000 : 500, 100);
                return 1;
            } 
        }
    }
    return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
}

// --- HIDDEN WINDOW: receives quit broadcast from new instance ---
LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_TGMIDSCROLL_QUIT) {
        isAutoScrolling = false;
        RestoreSystemCursor();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void CreateMessageWindow() {
    WNDCLASSA wc = {};
    wc.lpfnWndProc   = MsgWndProc;
    wc.hInstance      = GetModuleHandle(NULL);
    wc.lpszClassName  = "TgMidScroll_MsgWnd";
    RegisterClassA(&wc);
    hMsgWindow = CreateWindowA("TgMidScroll_MsgWnd", "", 0,
                                0, 0, 0, 0, HWND_MESSAGE, NULL,
                                GetModuleHandle(NULL), NULL);
}

int main() {
    WM_TGMIDSCROLL_QUIT = RegisterWindowMessageA("TgMidScroll_Quit");

    HANDLE hMutex = CreateMutexA(NULL, TRUE, "TgMidScroll_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        int result = MessageBoxA(NULL,
            "TgMidScroll is already running.\n\n"
            "Do you want to close the running instance?",
            "TgMidScroll",
            MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND);
        if (result == IDYES) {
            HWND hOld = FindWindowExA(HWND_MESSAGE, NULL, "TgMidScroll_MsgWnd", NULL);
            if (hOld) PostMessageA(hOld, WM_TGMIDSCROLL_QUIT, 0, 0);
        }
        return 0;
    }

    RegisterAutoStart();
    CreateMessageWindow();

    hMouseHook    = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc,
                                     GetModuleHandle(NULL), 0);
    hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                     GetModuleHandle(NULL), 0);
    if (!hMouseHook || !hKeyboardHook) return 1;

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    isAutoScrolling = false;
    if (hScrollThread) {
        WaitForSingleObject(hScrollThread, 500);
        CloseHandle(hScrollThread);
    }
    UnhookWindowsHookEx(hMouseHook);
    UnhookWindowsHookEx(hKeyboardHook);
    RestoreSystemCursor();
    CloseHandle(hMutex);
    return 0;
}
