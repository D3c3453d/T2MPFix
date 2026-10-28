/* QA command channel: the test harness writes qa_cmd.txt into the game folder; this runs the
 * commands on the game thread (polled from a PeekMessageA hook, so it works in menus too).
 *   click x y      left click at UI coords (game screen space, 800x600 here)
 *   move x y       move the UI cursor
 *   cmd <text>     run an engine command (e.g. "+walk", "quit_game")
 *   wait <ms>      pause the queue
 *   restore        un-minimize the game window without activating it
 *   status         write qa_status.txt
 * Mouse input is injected at the engine's mouse event queue (FUN_00689480, the Looking Glass
 * mouse event {short x, short y, u8 type, u32 time, u8 buttons}), because input posted from
 * outside the process is not reliable for a background window. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "MinHook.h"
#include "engine.h"
#include "common.h"

#define A_MOUSE_EVENT   0x689480   /* void (u32 xy, u32 type|time<<8, u32 time>>24|buttons<<8, u32) */
#define A_MOUSE_TIMEFN  0x86be28   /* u32 (*)(void): the mouse library's millisecond clock */
#define ME_MOTION 1
#define ME_LDOWN  2
#define ME_LUP    4

static char cmd_path[MAX_PATH], status_path[MAX_PATH];
static char queue[64][160];
static int q_n, q_i;
static double wait_until, last_tick;
static HWND game_wnd;

typedef BOOL(WINAPI *peek_t)(LPMSG, HWND, UINT, UINT, UINT);
static peek_t o_peek;
static WNDPROC o_wndproc;

/* The engine pauses its whole loop on WM_ACTIVATEAPP(FALSE). For tests with several instances on
 * one desktop, keep every instance running. */
static LRESULT CALLBACK hk_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if ((m == WM_ACTIVATEAPP && !w) || (m == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE) || m == WM_KILLFOCUS) return 0;
    return CallWindowProcA(o_wndproc, h, m, w, l);
}

static void mouse_event_(int x, int y, int type, int buttons)
{
    typedef void(__cdecl *ev_t)(uint32_t, uint32_t, uint32_t, uint32_t);
    typedef uint32_t(__cdecl *tm_t)(void);
    uint32_t ts = (*(tm_t *)VA(A_MOUSE_TIMEFN))();
    ((ev_t)VA(A_MOUSE_EVENT))((uint16_t)x | ((uint32_t)(uint16_t)y << 16), (uint32_t)type | (ts << 8),
                              (ts >> 24) | ((uint32_t)buttons << 8), 0);
}

static BOOL CALLBACK find_wnd(HWND h, LPARAM l)
{
    DWORD pid;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) { *(HWND *)l = h; return FALSE; }
    return TRUE;
}

static void write_status(void)
{
    FILE *f = fopen(status_path, "w");
    if (!f) return;
    fprintf(f, "time %.0f\nsim %u\nlocal_player %d\n", now_ms(), sim_ms(), qa_local_player());
    fclose(f);
}

static void run(const char *c)
{
    int x, y, ms;
    logf_("qa: %s", c);
    if (g_tel) fprintf(g_tel, "M,%.3f,%u,qa %s\n", now_ms(), sim_ms(), c);
    if (sscanf(c, "click %d %d", &x, &y) == 2) {
        mouse_event_(x, y, ME_MOTION, 0);
        mouse_event_(x, y, ME_LDOWN, 1);
        mouse_event_(x, y, ME_LUP, 0);
    } else if (sscanf(c, "move %d %d", &x, &y) == 2) {
        mouse_event_(x, y, ME_MOTION, 0);
    } else if (sscanf(c, "wait %d", &ms) == 1) {
        wait_until = now_ms() + ms;
    } else if (strncmp(c, "cmd ", 4) == 0) {
        run_verb(c + 4);
    } else if (strcmp(c, "restore") == 0) {
        if (!game_wnd) EnumWindows(find_wnd, (LPARAM)&game_wnd);
        if (game_wnd && IsIconic(game_wnd)) ShowWindow(game_wnd, SW_SHOWNOACTIVATE);
    } else if (strcmp(c, "activate") == 0) {
        if (game_wnd && o_wndproc) {
            CallWindowProcA(o_wndproc, game_wnd, WM_ACTIVATEAPP, TRUE, 0);
            CallWindowProcA(o_wndproc, game_wnd, WM_ACTIVATE, WA_ACTIVE, 0);
            CallWindowProcA(o_wndproc, game_wnd, WM_SETFOCUS, 0, 0);
        }
    } else if (strcmp(c, "status") == 0) {
        write_status();
    } else {
        logf_("qa: unknown command");
    }
}

static int qa_on;

static void tick(void)
{
    game_tick();
    if (!qa_on) return;
    double t = now_ms();
    if (t - last_tick < 50) return;
    last_tick = t;
    if (q_i >= q_n) {
        FILE *f = fopen(cmd_path, "r");
        if (!f) return;
        q_n = q_i = 0;
        while (q_n < 64 && fgets(queue[q_n], sizeof queue[0], f)) {
            queue[q_n][strcspn(queue[q_n], "\r\n")] = 0;
            if (queue[q_n][0] && queue[q_n][0] != '#') q_n++;
        }
        fclose(f);
        DeleteFileA(cmd_path);
    }
    while (q_i < q_n && t >= wait_until) run(queue[q_i++]);
    if (q_i >= q_n && q_n) { q_n = q_i = 0; write_status(); }
}

static BOOL WINAPI hk_peek(LPMSG m, HWND h, UINT a, UINT b, UINT r)
{
    static DWORD game_tid;
    if (!game_tid) {
        if (!game_wnd) EnumWindows(find_wnd, (LPARAM)&game_wnd);
        if (game_wnd) {
            game_tid = GetWindowThreadProcessId(game_wnd, NULL);
            if (qa_on) {
                o_wndproc = (WNDPROC)SetWindowLongPtrA(game_wnd, GWLP_WNDPROC, (LONG_PTR)hk_wndproc);
                logf_("qa: game window %p subclassed", (void *)game_wnd);
            }
        }
    }
    if (game_tid && GetCurrentThreadId() == game_tid) tick();
    return o_peek(m, h, a, b, r);
}

void qa_init(const char *dir, int enabled, int need_tick)
{
    qa_on = enabled;
    snprintf(cmd_path, sizeof cmd_path, "%sqa_cmd.txt", dir);
    snprintf(status_path, sizeof status_path, "%sqa_status.txt", dir);
    DeleteFileA(cmd_path);
    MH_STATUS s = MH_CreateHookApi(L"user32", "PeekMessageA", hk_peek, (void **)&o_peek);
    if (s == MH_OK) s = MH_EnableHook(MH_ALL_HOOKS);
    logf_("tick hook: %s%s%s", MH_StatusToString(s), need_tick ? ", fix ticks" : "", qa_on ? ", QA channel on" : "");
}
