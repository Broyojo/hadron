/* win-info: prints what Windows knows about another program's top-level windows, to compare
 * with what macOS shows for them (mac/ax-window). Run it in the same Wine prefix as the program.
 *
 *   win-info.exe <part of the window title> [restore|maximize]
 *
 * For each visible top-level window whose title contains the text: style, extended style,
 * window and client rects, whether it has a window region (a shape), whether it is maximized or
 * minimized, and its DPI. With restore or maximize, it then sends the window that command, as a
 * double click on its title bar would, and prints it again.
 *
 * Wine doesn't give out another program's restore rect (GetWindowPlacement returns the window's
 * current rect for it), so to see where a window restores to, restore it.
  *
 * Build: toolchains/llvm-mingw/bin/x86_64-w64-mingw32-clang -Os -municode -o win-info.exe tests/win/win-info.c -luser32 -lgdi32
 */
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static const wchar_t *needle;
static WPARAM command;

static const char *region_kind(int kind)
{
    switch (kind)
    {
    case NULLREGION: return "empty";
    case SIMPLEREGION: return "a rectangle";
    case COMPLEXREGION: return "a complex shape";
    default: return "none";
    }
}

static void print_rect(const char *label, RECT r)
{
    printf("  %-16s %ld,%ld to %ld,%ld (%ldx%ld)\n", label, r.left, r.top, r.right, r.bottom,
           r.right - r.left, r.bottom - r.top);
}

static BOOL CALLBACK each_window(HWND hwnd, LPARAM lparam)
{
    (void)lparam;
    if (!IsWindowVisible(hwnd)) return TRUE;
    /* the whole title, however long */
    int length = GetWindowTextLengthW(hwnd);
    wchar_t *title = calloc(length + 1, sizeof(*title));
    if (!title)
    {
        printf("no memory for a title of %d characters\n", length);
        exit(1);
    }
    GetWindowTextW(hwnd, title, length + 1);
    if (!wcsstr(title, needle))
    {
        free(title);
        return TRUE;
    }

    LONG style = GetWindowLongW(hwnd, GWL_STYLE), exstyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    RECT window, client, region_box;
    GetWindowRect(hwnd, &window);
    GetClientRect(hwnd, &client);
    MapWindowPoints(hwnd, NULL, (POINT *)&client, 2);

    HRGN region = CreateRectRgn(0, 0, 0, 0);
    int kind = GetWindowRgn(hwnd, region);

    WINDOWPLACEMENT placement = {.length = sizeof(placement)};
    GetWindowPlacement(hwnd, &placement);

    printf("window %p \"%ls\"\n", (void *)hwnd, title);
    printf("  style %08lx:%s%s%s%s%s%s%s\n", (unsigned long)style,
           (style & WS_POPUP) ? " popup" : "", (style & WS_CAPTION) == WS_CAPTION ? " caption" : "",
           (style & WS_THICKFRAME) ? " sizing-border" : "", (style & WS_SYSMENU) ? " sysmenu" : "",
           (style & WS_MAXIMIZEBOX) ? " maximize-box" : "", (style & WS_MAXIMIZE) ? " MAXIMIZED" : "",
           (style & WS_MINIMIZE) ? " MINIMIZED" : "");
    printf("  extended style %08lx%s%s\n", (unsigned long)exstyle,
           (exstyle & WS_EX_LAYERED) ? " layered" : "", (exstyle & WS_EX_TOOLWINDOW) ? " tool-window" : "");
    print_rect("window", window);
    print_rect("client", client);
    printf("  region           %s", region_kind(kind));
    if (kind == SIMPLEREGION || kind == COMPLEXREGION)
    {
        GetRgnBox(region, &region_box);
        printf(", bounds %ld,%ld to %ld,%ld", region_box.left, region_box.top, region_box.right, region_box.bottom);
    }
    printf("\n");
    DeleteObject(region);
    printf("  placement        %s\n", placement.showCmd == SW_SHOWMAXIMIZED ? "maximized"
                                     : placement.showCmd == SW_SHOWMINIMIZED ? "minimized" : "normal");
    printf("  dpi              %u\n", GetDpiForWindow(hwnd));
    if (command)
    {
        WPARAM sent = command;

        printf("sending %s\n", command == SC_RESTORE ? "restore" : "maximize");
        PostMessageW(hwnd, WM_SYSCOMMAND, command, 0);
        Sleep(1500);
        /* print it again without sending the command again; the next matching window gets it too */
        command = 0;
        each_window(hwnd, 0);
        command = sent;
    }
    free(title);
    return TRUE;
}

/* wmain: the title part arrives as Windows has it, whatever the ANSI code page */
int wmain(int argc, wchar_t **argv)
{
    if (argc < 2 || argc > 3 || (argc == 3 && wcscmp(argv[2], L"restore") && wcscmp(argv[2], L"maximize")))
    {
        printf("usage: win-info.exe <part of the window title> [restore|maximize]\n");
        return 2;
    }
    if (argc == 3) command = !wcscmp(argv[2], L"restore") ? SC_RESTORE : SC_MAXIMIZE;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    needle = argv[1];
    EnumWindows(each_window, 0);
    fflush(stdout);
    return 0;
}
