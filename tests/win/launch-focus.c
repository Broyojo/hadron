#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HANDLE output;
static HWND window;
static void log_state(const char *phase)
{
    char text[512];
    DWORD written;
    int length = wsprintfA(text, "phase=%s tick=%lu pid=%lu hwnd=%p fg=%p active=%p focus=%p visible=%d minimized=%d\r\n",
        phase, GetTickCount(), GetCurrentProcessId(), window, GetForegroundWindow(),
        GetActiveWindow(), GetFocus(), IsWindowVisible(window), IsIconic(window));
    WriteFile(output, text, length, &written, NULL);
    FlushFileBuffers(output);
}
static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_ACTIVATE || message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ACTIVATEAPP)
    {
        window = hwnd;
        log_state(message == WM_ACTIVATE ? "WM_ACTIVATE" : message == WM_SETFOCUS ? "WM_SETFOCUS" :
            message == WM_KILLFOCUS ? "WM_KILLFOCUS" : "WM_ACTIVATEAPP");
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
static void pump(DWORD milliseconds)
{
    DWORD start = GetTickCount();
    MSG message;
    while (GetTickCount() - start < milliseconds)
    {
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        Sleep(10);
    }
}
void start(void)
{
    WNDCLASSW cls = {0};
    output = CreateFileA("win32-launch.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (output == INVALID_HANDLE_VALUE) ExitProcess(2);
    cls.lpfnWndProc = procedure;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"HadronLaunchFocusProbe";
    if (!RegisterClassW(&cls)) ExitProcess(2);
    DWORD delay = GetEnvironmentVariableA("WINE_LAUNCH_PROBE_DELAY", NULL, 0) ? 1200 : 0;
    log_state("before-window");
    pump(delay);
    BOOL full = GetEnvironmentVariableA("WINE_LAUNCH_PROBE_FULLSCREEN", NULL, 0) != 0;
    BOOL top = GetEnvironmentVariableA("WINE_LAUNCH_PROBE_TOPMOST", NULL, 0) != 0;
    BOOL transition = GetEnvironmentVariableA("WINE_LAUNCH_PROBE_TRANSITION", NULL, 0) != 0;
    window = CreateWindowExW(top ? WS_EX_TOPMOST : 0, cls.lpszClassName, L"Hadron launch focus probe",
        full ? WS_POPUP : WS_OVERLAPPEDWINDOW, full ? 0 : 80, full ? 0 : 80,
        full ? GetSystemMetrics(SM_CXSCREEN) - (transition ? 40 : 0) : 500,
        full ? GetSystemMetrics(SM_CYSCREEN) - (transition ? 40 : 0) : 300,
        NULL, NULL, cls.hInstance, NULL);
    if (!window) ExitProcess(2);
    ShowWindow(window, SW_SHOW);
    log_state("shown");
    SetForegroundWindow(window);
    log_state("foreground-requested");
    if (transition)
    {
        pump(500);
        SetWindowPos(window, NULL, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                     SWP_NOACTIVATE | SWP_NOZORDER);
        log_state("fullscreen-transition");
    }
    pump(700);
    log_state("settled");
    pump(1300);
    log_state("before-close");
    DestroyWindow(window);
    CloseHandle(output);
    ExitProcess(0);
}
