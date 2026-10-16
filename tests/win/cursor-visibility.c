/* Manual native-cursor regression probe. Run through scripts/play.
 * The pointer should stay invisible at the screen edges for twenty seconds,
 * then become visible normally when the window closes. No screen capture. */
#include <windows.h>

static HCURSOR previous_cursor;

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        FillRect(dc, &paint.rcPaint, (HBRUSH)GetStockObject(BLACK_BRUSH));
        EndPaint(window, &paint);
        return 0;
    }
    case WM_SETCURSOR:
        SetCursor(NULL);
        return TRUE;
    case WM_TIMER:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        ClipCursor(NULL);
        SetCursor(previous_cursor);
        ShowCursor(TRUE);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, WCHAR *command, int show)
{
    WNDCLASSW cls = {0};
    RECT bounds = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    HWND window;
    MSG message;

    cls.hInstance = instance;
    cls.lpfnWndProc = window_proc;
    cls.lpszClassName = L"HadronCursorProbe";
    cls.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    if (!RegisterClassW(&cls)) return 1;
    window = CreateWindowExW(WS_EX_TOPMOST, cls.lpszClassName, L"Hadron cursor boundary probe",
                             WS_POPUP, 0, 0, bounds.right, bounds.bottom,
                             NULL, NULL, instance, NULL);
    if (!window) return 2;
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    previous_cursor = SetCursor(NULL);
    ShowCursor(FALSE);
    ClipCursor(&bounds);
    if (!SetTimer(window, 1, 20000, NULL))
    {
        DestroyWindow(window);
        return 3;
    }
    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
