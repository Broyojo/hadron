/* scroll-target: does a sideways swipe on the trackpad scroll a Windows app sideways? A window with
 * a view inside, as in a diagram editor, counts what reaches the view for a while, then says how
 * far it scrolled each way. Swipe on it, or let it scroll itself:
 *
 *   scroll-target.exe legacy|hwheel [toplevel|thread|nested] [send-right|send-down] [SECONDS]
 *
 * send-right and send-down post ten small wheel turns to the view, sideways or down, as Wine does
 * for a trackpad swipe. (Posted, not put through SendInput: Wine sends that wheel to the window under
 * the pointer, as Windows does, and the pointer may be anywhere. And
 * macOS gives scroll events that one app posts to another no window, so Wine's Mac driver drops
 * them. A real swipe is up to a person.)
 *
 * legacy: the view understands only the up-and-down wheel, and Shift + wheel as sideways, as many
 * older apps do (Enterprise Architect, for one); the sideways wheel (WM_MOUSEHWHEEL) goes to DefWindowProc.
 * hwheel: the view handles the sideways wheel itself (the control). toplevel: no view, the main
 * window scrolls. thread: the view belongs to a thread of its own, not the main window's. nested:
 * the main window takes the view's sideways wheel and, while it does, sends it on to a second view,
 * as an app that keeps two views side by side might. SECONDS: how long it listens (default 8).
 *
 * PASS: a sideways scroll to the right moved the view right and not up or down (with send-down: down
 * and not sideways); with nested, it moved the second view right and the first not at all. Exit
 * status 0 or 1.
 *
 * Build: toolchains/llvm-mingw/bin/x86_64-w64-mingw32-clang -Os -o scroll-target.exe tests/win/scroll-target.c -luser32 -lgdi32
 */
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>
#include <string.h>

static BOOL handles_hwheel, toplevel, own_thread, nested, in_nested;
static UINT send_message;
static HWND main_window, view, second_view;
static HANDLE view_ready;
static long scroll_x, scroll_y, second_x;
static wchar_t last_event[128] = L"Scroll over this window.";

/* what the window shows, live: the last wheel message and the totals */
static void show(HWND hwnd, const wchar_t *what, int delta)
{
    swprintf(last_event, ARRAYSIZE(last_event), L"last: %ls %+d", what, delta);
    printf("%ls %+d (scrolled %ld right, %ld down)\n", what, delta, scroll_x, scroll_y);
    fflush(stdout);
    InvalidateRect(GetAncestor(hwnd, GA_ROOT), NULL, TRUE);
    if (view) InvalidateRect(view, NULL, TRUE);
}
static int hwheel_seen, hwheel_at_top, shift_wheel, shift_key_down, plain_wheel, hscroll;

static LRESULT scroll_messages(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, BOOL *handled)
{
    int delta = GET_WHEEL_DELTA_WPARAM(wparam);
    *handled = TRUE;
    switch (msg)
    {
    case WM_MOUSEWHEEL:
        if (GET_KEYSTATE_WPARAM(wparam) & MK_SHIFT)
        {
            /* wheel up is to the left */
            shift_wheel++;
            if (GetKeyState(VK_SHIFT) < 0) shift_key_down++;
            if (hwnd == second_view) second_x -= delta;
            else scroll_x -= delta;
            show(hwnd, L"Shift + wheel", delta);
        }
        else
        {
            plain_wheel++;
            scroll_y -= delta;
            show(hwnd, L"wheel", delta);
        }
        return 0;
    case WM_MOUSEHWHEEL:
        hwheel_seen++;
        if (handles_hwheel)
        {
            scroll_x += delta;
            show(hwnd, L"sideways wheel", delta);
            return 0;
        }
        break;
    case WM_HSCROLL:
        hscroll++;
        if (LOWORD(wparam) == SB_LINERIGHT) scroll_x += 40;
        if (LOWORD(wparam) == SB_LINELEFT) scroll_x -= 40;
        show(hwnd, L"WM_HSCROLL", 0);
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        wchar_t text[256];
        HDC hdc = BeginPaint(hwnd, &ps);
        int dpi = GetDpiForWindow(hwnd);
        HFONT font = CreateFontW(-MulDiv(16, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Tahoma");
        HGDIOBJ old = SelectObject(hdc, font);
        RECT rect;
        GetClientRect(hwnd, &rect);
        InflateRect(&rect, -MulDiv(16, dpi, 96), -MulDiv(16, dpi, 96));
        swprintf(text, ARRAYSIZE(text), L"%ls\n\nscrolled %ld right, %ld down\n(one wheel notch is 120)",
                 last_event, scroll_x, scroll_y);
        SetBkMode(hdc, TRANSPARENT);
        DrawTextW(hdc, text, -1, &rect, DT_LEFT | DT_TOP);
        SelectObject(hdc, old);
        DeleteObject(font);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    (void)hwnd; (void)lparam;
    *handled = FALSE;
    return 0;
}

static LRESULT CALLBACK view_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    BOOL handled;
    LRESULT ret = scroll_messages(hwnd, msg, wparam, lparam, &handled);
    return handled ? ret : DefWindowProcW(hwnd, msg, wparam, lparam);
}

static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    BOOL handled;
    LRESULT ret;

    if (toplevel)
    {
        ret = scroll_messages(hwnd, msg, wparam, lparam, &handled);
        if (handled) return ret;
    }
    switch (msg)
    {
    case WM_MOUSEHWHEEL:
        if (nested && !in_nested)
        {
            /* take it, and send it on to the second view, which leaves it to DefWindowProc */
            in_nested = TRUE;
            SendMessageW(second_view, WM_MOUSEHWHEEL, wparam, lparam);
            in_nested = FALSE;
            return 0;
        }
        if (!toplevel) hwheel_at_top++;
        break;
    case WM_SIZE:
        if (view) MoveWindow(view, 0, 0, LOWORD(lparam) / (nested ? 2 : 1), HIWORD(lparam), TRUE);
        if (second_view) MoveWindow(second_view, LOWORD(lparam) / 2, 0, LOWORD(lparam) / 2, HIWORD(lparam), TRUE);
        return 0;
    case WM_TIMER:
        if (wparam == 2)
        {
            HWND target = view ? view : hwnd;
            RECT rect;
            KillTimer(hwnd, 2);
            GetWindowRect(target, &rect);
            for (int i = 0; i < 10; i++)
                PostMessageW(target, send_message, MAKEWPARAM(0, send_message == WM_MOUSEHWHEEL ? 40 : -40), /* right; down */
                             MAKELPARAM((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2));
            return 0;
        }
        PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static HWND create_view(const wchar_t *name, int x, int width)
{
    RECT client;
    GetClientRect(main_window, &client);
    return CreateWindowExW(0, L"scroll-target view", name, WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL,
                           x, 0, width ? width : client.right, client.bottom, main_window, NULL,
                           GetModuleHandleW(NULL), NULL);
}

/* thread: the view, and the messages that reach it, on a thread of its own */
static DWORD WINAPI view_thread(void *arg)
{
    MSG msg;
    view = create_view(L"view", 0, 0);
    SetEvent(view_ready);
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    (void)arg;
    return 0;
}

int main(int argc, char **argv)
{
    int seconds = 8;
    wchar_t title[64];
    HANDLE thread = NULL;
    DWORD thread_id;
    MSG msg;
    BOOL pass;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "hwheel")) handles_hwheel = TRUE;
        else if (!strcmp(argv[i], "legacy")) handles_hwheel = FALSE;
        else if (!strcmp(argv[i], "toplevel")) toplevel = TRUE;
        else if (!strcmp(argv[i], "thread")) own_thread = TRUE;
        else if (!strcmp(argv[i], "nested")) nested = TRUE;
        else if (!strcmp(argv[i], "send-right")) send_message = WM_MOUSEHWHEEL;
        else if (!strcmp(argv[i], "send-down")) send_message = WM_MOUSEWHEEL;
        else if (atoi(argv[i]) > 0) seconds = atoi(argv[i]);
        else
        {
            printf("usage: scroll-target.exe legacy|hwheel [toplevel|thread|nested] [send-right|send-down] [SECONDS]\n");
            return 2;
        }
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSW main_class = {.lpfnWndProc = main_proc, .hInstance = GetModuleHandleW(NULL),
                            .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW),
                            .hbrBackground = (HBRUSH)(COLOR_WINDOW + 1), .lpszClassName = L"scroll-target"};
    WNDCLASSW view_class = {.lpfnWndProc = view_proc, .hInstance = GetModuleHandleW(NULL),
                            .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW),
                            .hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1), .lpszClassName = L"scroll-target view"};
    RegisterClassW(&main_class);
    RegisterClassW(&view_class);

    swprintf(title, 64, L"scroll-target (%ls%ls)", handles_hwheel ? L"hwheel" : L"legacy",
             toplevel ? L", toplevel" : own_thread ? L", thread" : nested ? L", nested" : L"");
    main_window = CreateWindowExW(0, L"scroll-target", title, WS_OVERLAPPEDWINDOW | (toplevel ? WS_HSCROLL | WS_VSCROLL : 0),
                                  200, 200, 1200, 800, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (own_thread)
    {
        view_ready = CreateEventW(NULL, FALSE, FALSE, NULL);
        thread = CreateThread(NULL, 0, view_thread, NULL, 0, &thread_id);
        /* creating the view sends the main window WM_PARENTNOTIFY: keep taking messages meanwhile */
        while (MsgWaitForMultipleObjects(1, &view_ready, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0)
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    }
    else if (nested)
    {
        RECT client;
        GetClientRect(main_window, &client);
        view = create_view(L"view", 0, client.right / 2);
        second_view = create_view(L"second view", client.right / 2, client.right / 2);
    }
    else if (!toplevel) view = create_view(L"view", 0, 0);
    ShowWindow(main_window, SW_SHOWNORMAL);
    SetForegroundWindow(main_window);
    SetFocus(view ? view : main_window);
    SetTimer(main_window, 1, seconds * 1000, NULL);
    if (send_message) SetTimer(main_window, 2, 1500, NULL);

    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (thread)
    {
        PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
        WaitForSingleObject(thread, INFINITE);
    }

    if (send_message == WM_MOUSEWHEEL) pass = scroll_y > 0 && scroll_x == 0;
    else if (nested) pass = second_x > 0 && scroll_x == 0 && scroll_y == 0;
    else pass = scroll_x > 0 && scroll_y == 0;
    if (nested) printf("the second view scrolled %ld to the right\n", second_x);
    printf("sideways wheel: %d to the %s, %d of them unhandled at the main window\n", hwheel_seen,
           toplevel ? "main window" : "view", hwheel_at_top);
    printf("Shift + wheel: %d (Shift down by GetKeyState: %d); wheel: %d; WM_HSCROLL: %d\n",
           shift_wheel, shift_key_down, plain_wheel, hscroll);
    printf("scrolled %ld to the right, %ld down: %s\n", scroll_x, scroll_y, pass ? "PASS" : "FAIL");
    fflush(stdout);
    return pass ? 0 : 1;
}
