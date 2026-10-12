#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* No CRT is needed: build with -nostdlib and -Wl,/entry:start. */
static HANDLE output;
static unsigned failures, checks;

static void write_line(const char *line, DWORD length)
{
    DWORD written;
    if (!WriteFile(output, line, length, &written, NULL) || written != length ||
        !FlushFileBuffers(output)) ExitProcess(90);
}

static void api_failure(const char *operation)
{
    char line[256];
    DWORD error = GetLastError();
    int length = wsprintfA(line, "FAIL api=%s error=%lu\r\n", operation, error);
    write_line(line, length);
    CloseHandle(output);
    ExitProcess(2);
}

static void record(const char *phase, HWND window, BOOL expected_iconic, BOOL expected_visible)
{
    RECT rect = {0};
    char line[512];
    BOOL rect_ok = GetClientRect(window, &rect);
    DWORD error = rect_ok ? 0 : GetLastError();
    BOOL iconic = IsIconic(window), visible = IsWindowVisible(window);
    BOOL ok = iconic == expected_iconic && visible == expected_visible && rect_ok;
    if (!expected_iconic && expected_visible)
        ok = ok && rect.right > rect.left && rect.bottom > rect.top;
    checks++;
    if (!ok) failures++;
    int length = wsprintfA(line,
        "phase=%s iconic=%d visible=%d client=%ldx%ld rect_ok=%d error=%lu "
        "expected_iconic=%d expected_visible=%d result=%s\r\n",
        phase, iconic, visible, rect.right - rect.left, rect.bottom - rect.top,
        rect_ok, error, expected_iconic, expected_visible, ok ? "PASS" : "FAIL");
    write_line(line, length);
}

static void pump(DWORD milliseconds)
{
    DWORD start = GetTickCount();
    MSG message;
    while (GetTickCount() - start < milliseconds)
    {
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(window, message, wparam, lparam);
}

static HWND create(DWORD extra)
{
    HWND window = CreateWindowExW(0, L"HadronMinimizeLifecycle", L"Hadron minimization checks",
        WS_OVERLAPPEDWINDOW | extra, 100, 100, 480, 300, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!window) api_failure("CreateWindowExW");
    return window;
}

static void destroy(HWND window)
{
    if (!DestroyWindow(window)) api_failure("DestroyWindow");
    pump(300);
}

void start(void)
{
    static WNDCLASSW wc;
    HWND window;
    char line[128];
    output = CreateFileA("results.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE) ExitProcess(90);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"HadronMinimizeLifecycle";
    if (!RegisterClassW(&wc)) api_failure("RegisterClassW");

    window = create(WS_MINIMIZE);
    pump(400); record("created-hidden-minimized", window, TRUE, FALSE);
    ShowWindow(window, SW_SHOWNA);
    pump(1000); record("shown-still-minimized", window, TRUE, TRUE);
    ShowWindow(window, SW_RESTORE);
    pump(1000); record("restored-after-deferred", window, FALSE, TRUE);
    destroy(window);

    window = create(0);
    ShowWindow(window, SW_SHOWNORMAL);
    pump(600); record("ordinary-visible", window, FALSE, TRUE);
    ShowWindow(window, SW_MINIMIZE);
    pump(1000); record("ordinary-minimized", window, TRUE, TRUE);
    ShowWindow(window, SW_HIDE);
    pump(400); record("hidden-while-minimized", window, TRUE, FALSE);
    ShowWindow(window, SW_SHOWNA);
    pump(1000); record("reshown-still-minimized", window, TRUE, TRUE);
    ShowWindow(window, SW_RESTORE);
    pump(1000); record("restored-after-reshow", window, FALSE, TRUE);
    destroy(window);

    window = create(WS_MINIMIZE);
    pump(400); record("cancel-created-hidden-minimized", window, TRUE, FALSE);
    ShowWindow(window, SW_RESTORE);
    pump(1000); record("cancel-restored-directly", window, FALSE, TRUE);
    pump(1000); record("cancel-remains-restored", window, FALSE, TRUE);
    destroy(window);

    int length = wsprintfA(line, "SUMMARY checks=%u expected=11 failures=%u\r\n", checks, failures);
    write_line(line, length);
    CloseHandle(output);
    ExitProcess(failures || checks != 11 ? 1 : 0);
}
