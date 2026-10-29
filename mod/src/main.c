/* T2MPFix: fixes for Thief 2 NewDark 1.29 multiplayer, as independent modules (see fixes.h):
 *  - smooth remote players: send_fix (fix_send.c) + recv_fix (fix_recv.c);
 *  - stock steering for players that aren't interpolated: steer_fix (fix_steer.c);
 *  - the player limit: max_players (fix_players.c);
 *  - damage from local-only hazards (e.g. lava): damage_fix (fix_damage.c).
 * This file is the core: the exe check, config, log, engine helpers, the ghost hooks the avatar fixes plug
 * into (also used by QA telemetry), and the QA bot and network impairment.
 *
 * Stock behaviour (see docs/NOTES.md):
 *  - the sender only sends a player heartbeat when the receiver's dead-reckoned guess is off by
 *    more than 4 units on an axis;
 *  - the receiver dead-reckons with gravity and a raycast that stops at walls, then *steers* the
 *    avatar's physics body toward that guess with a velocity (gain 2/s), colliding with the world
 *    on the way, and only teleports when the error exceeds 12 units.
 * Result: avatars that lag, stick on doorframes and stairs, float, sink, or vanish into walls.
 */
#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "MinHook.h"
#include "engine.h"
#include "fixes.h"
#include "common.h"

uintptr_t g_base;
int g_hook_mask;

static const fix_t *const fixes[] = { &fix_send, &fix_recv, &fix_steer, &fix_players, &fix_damage };
#define N_FIXES (int)(sizeof fixes / sizeof fixes[0])
static int fix_on[N_FIXES];

int (*ghost_send_force)(uint8_t *lg, uint8_t *hb);
void (*ghost_steer_pre)(uint8_t *rg, double now, int new_hb, steer_info_t *si);
void (*ghost_steer_post)(uint8_t *rg, const float target[3], steer_info_t *si);

static struct {
    int telemetry, qa;
    int net_delay_ms, net_jitter_ms, net_loss_pct, net_seed;   /* QA: impair incoming heartbeats */
    char bot[MAX_PATH];
} cfg;

static FILE *g_log;
FILE *g_tel;
static LARGE_INTEGER g_qpf;
static int g_local_player;          /* obj id of our player (from the local ghost table) */
static unsigned g_frames;
int qa_local_player(void) { return g_local_player; }

/* ---------- utilities ---------- */

double now_ms(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)g_qpf.QuadPart;
}

void logf_(const char *fmt, ...)
{
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(g_log, "[%10.1f] ", now_ms());
    vfprintf(g_log, fmt, ap);
    fputc('\n', g_log);
    fflush(g_log);
    va_end(ap);
}

uint32_t sim_ms(void) { return *(uint32_t *)VA(A_SIM_TIME_MS); }

float len3(const float *v) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

typedef uint8_t *(__stdcall *phys_get_model_t)(int obj);   /* ret 4 */
float *model_pos(int obj)
{
    uint8_t *m = ((phys_get_model_t)VA(A_PHYS_GET_MODEL))(obj);
    return m ? (float *)(m + PM_POS) : NULL;
}

void command_execute(const char *cmd)
{
    char buf[256];
    lstrcpynA(buf, cmd, sizeof buf);
    void *f = VA(A_COMMAND_EXECUTE);
    __asm__ volatile("call *%1" : : "a"(buf), "r"(f) : "ecx", "edx", "memory", "cc");
}

int16_t *obj_facing(int obj)
{
    uint8_t *tab = *(uint8_t **)VA(A_POSITION_TABLE);
    if (!tab || obj < 0 || (uint32_t)obj >= *(uint32_t *)(tab + 0x20)) return NULL;
    uint8_t *pos = (*(uint8_t ***)(tab + 0x18))[obj];
    return pos ? (int16_t *)(pos + 0x10) : NULL;
}

/* Unconditional placement (no "does the body fit" check), used when the engine's warp refuses. */
void obj_pos_update(int obj, const float *pos, const int16_t *facing)
{
    void *f = VA(A_OBJ_POS_UPDATE);
    __asm__ volatile("push %3\n\tcall *%2\n\tadd $4, %%esp"
                     : "+c"(pos), "+d"(obj)
                     : "r"(f), "r"(facing)
                     : "eax", "memory", "cc");
}

/* Input binder command, e.g. "forward 2", "turn -1", "jump": what a bound key would run. */
void binder_cmd(const char *cmd)
{
    void **binder = *(void ***)VA(A_INPUT_BINDER);
    if (!binder) return;
    char buf[256];
    lstrcpynA(buf, cmd, sizeof buf);
    typedef long(__stdcall * pc_t)(void *, char *);
    ((pc_t)((void **)*binder)[0x30 / 4])(binder, buf);
}

/* Bot and QA verbs: "bind <binder command>", "mark <text>" (telemetry only), else engine command. */
void run_verb(const char *c)
{
    if (strncmp(c, "bind ", 5) == 0) binder_cmd(c + 5);
    else if (strncmp(c, "mark ", 5) != 0) command_execute(c);
}

/* Called from the message loop hook (menus and in game). */
void game_tick(void)
{
    for (int i = 0; i < N_FIXES; i++)
        if (fix_on[i] && fixes[i]->tick) fixes[i]->tick();
}

/* ---------- bot: timed engine commands, for automated QA ---------- */

static struct { double at; char cmd[120]; } bot_steps[512];
static int bot_n, bot_next;
static double bot_t0, bot_loop_ms;

static void bot_load(void)
{
    if (!cfg.bot[0]) return;
    FILE *f = fopen(cfg.bot, "r");
    if (!f) { logf_("bot: cannot open %s", cfg.bot); return; }
    char line[200];
    while (fgets(line, sizeof line, f) && bot_n < 512) {
        double at; char cmd[120];
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "loop %lf", &at) == 1) { bot_loop_ms = at; continue; }
        if (sscanf(line, "%lf %119[^\r\n]", &at, cmd) == 2) {
            bot_steps[bot_n].at = at;
            lstrcpynA(bot_steps[bot_n].cmd, cmd, 120);
            bot_n++;
        }
    }
    fclose(f);
    logf_("bot: %d steps from %s, loop %.0f ms", bot_n, cfg.bot, bot_loop_ms);
}

static void bot_tick(void)
{
    if (!bot_n || !g_local_player) return;
    double t = now_ms();
    if (bot_t0 == 0) bot_t0 = t;
    while (bot_next < bot_n && t - bot_t0 >= bot_steps[bot_next].at) {
        const char *c = bot_steps[bot_next].cmd;
        if (g_tel) fprintf(g_tel, "M,%.3f,%u,%s\n", t, sim_ms(), c);
        run_verb(c);
        bot_next++;
    }
    if (bot_next == bot_n && bot_loop_ms > 0 && t - bot_t0 >= bot_loop_ms) { bot_t0 = t; bot_next = 0; }
}

/* ---------- QA network impairment: delay, jitter and drop incoming ghost heartbeats ----------
 * The game's transport is DirectPlay (no plain socket imports), so instead of a UDP relay the
 * impairment sits in front of the stock heartbeat receiver. Both machines apply it, so each
 * direction sees the configured one-way delay. Deterministic for a given seed. */

typedef void(__cdecl *recv_hb_t)(int obj, int seq, int relobj, uint8_t *hb, int16_t *extra);
static recv_hb_t o_recv_hb;

typedef struct { double due; int obj, seq, relobj, has_extra; uint8_t hb[0x28]; int16_t extra[2]; } pending_t;
static pending_t pend[256];
static int pend_n;
static uint32_t rng_state;
static unsigned net_dropped, net_delayed;

static int netsim_on(void) { return cfg.net_delay_ms || cfg.net_jitter_ms || cfg.net_loss_pct; }

static uint32_t rng(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }

static void __cdecl hk_recv_hb(int obj, int seq, int relobj, uint8_t *hb, int16_t *extra)
{
    if (!rng_state) rng_state = (uint32_t)cfg.net_seed * 2654435761u + 1;
    if ((int)(rng() % 100) < cfg.net_loss_pct) { net_dropped++; return; }
    if (pend_n == 256) { net_dropped++; return; }
    pending_t *p = &pend[pend_n++];
    double j = cfg.net_jitter_ms ? (double)(rng() % (2 * cfg.net_jitter_ms + 1)) - cfg.net_jitter_ms : 0;
    p->due = now_ms() + cfg.net_delay_ms + j;
    p->obj = obj; p->seq = seq; p->relobj = relobj;
    memcpy(p->hb, hb, sizeof p->hb);
    p->has_extra = extra != NULL;
    if (extra) memcpy(p->extra, extra, sizeof p->extra);
    net_delayed++;
}

static void netsim_deliver(void)
{
    double t = now_ms();
    for (int i = 0; i < pend_n;) {
        if (pend[i].due <= t) {
            pending_t p = pend[i];
            pend[i] = pend[--pend_n];        /* order not kept: jitter reorders, like a real network */
            o_recv_hb(p.obj, p.seq, p.relobj, p.hb, p.has_extra ? p.extra : NULL);
        } else {
            i++;
        }
    }
}

/* ---------- ghost hooks: telemetry, local player, and the avatar fixes' extension points ---------- */

static void tel_remote_frame(int after);

typedef void(__cdecl *ghost_frame_t)(int dt_ms);
typedef int(__cdecl *send_decide_t)(uint8_t *lg, uint8_t *hb);
typedef void(__cdecl *send_t)(uint8_t *lg, uint8_t *hb);
typedef void(__cdecl *steer_t)(uint8_t *rg, float dt);
static ghost_frame_t o_ghost_frame;
static send_decide_t o_send_decide;
static send_t o_send;
static steer_t o_steer;

static void __cdecl hk_ghost_frame(int dt_ms)
{
    if (o_recv_hb) netsim_deliver();
    if (g_tel && !o_steer) tel_remote_frame(0);
    o_ghost_frame(dt_ms);
    if (g_tel && !o_steer) tel_remote_frame(1);
    g_frames++;
    bot_tick();
    if (g_tel && g_local_player) {
        float *p = model_pos(g_local_player);
        if (p) fprintf(g_tel, "L,%.3f,%u,%d,%.3f,%.3f,%.3f\n", now_ms(), sim_ms(), g_local_player, p[0], p[1], p[2]);
    }
    if (g_tel && (g_frames % 120) == 0) fflush(g_tel);
}

static int __cdecl hk_send_decide(uint8_t *lg, uint8_t *hb)
{
    int r = o_send_decide(lg, hb);
    if ((FLD(lg, LG_FLAGS, uint32_t) & GF_TYPE_MASK) != GF_TYPE_PLAYER) return r;
    int obj = FLD(lg, LG_OBJ, int);
    if (g_local_player != obj) { g_local_player = obj; logf_("local player obj %d", obj); }
    if (!r && ghost_send_force) r = ghost_send_force(lg, hb);
    return r;
}

static void __cdecl hk_send(uint8_t *lg, uint8_t *hb)
{
    o_send(lg, hb);
    if (g_tel && (FLD(lg, LG_FLAGS, uint32_t) & GF_TYPE_MASK) == GF_TYPE_PLAYER) {
        float *p = (float *)(hb + HB_POS);
        fprintf(g_tel, "S,%.3f,%u,%d,%u,%.3f,%.3f,%.3f,%04x,%u\n", now_ms(), sim_ms(), FLD(lg, LG_OBJ, int),
                FLD(lg, LG_SEQ, uint16_t), p[0], p[1], p[2], FLD(hb, HB_FLAGS, uint16_t), FLD(hb, HB_MODE, uint8_t));
    }
}

/* Last heartbeat seen per remote player, to tell a new one from a repeat. */
static struct { int obj; uint16_t seq; uint32_t recv_ms; } seen[16];

static int new_heartbeat(int obj, uint16_t seq, uint32_t rms)
{
    int slot = -1;
    for (int i = 0; i < 16 && slot < 0; i++)
        if (seen[i].obj == obj) slot = i;
    for (int i = 0; i < 16 && slot < 0; i++)
        if (!seen[i].obj) slot = i;
    if (slot < 0) slot = (unsigned)obj % 16;
    if (seen[slot].obj == obj && seen[slot].seq == seq && seen[slot].recv_ms == rms) return 0;
    seen[slot].obj = obj;
    seen[slot].seq = seq;
    seen[slot].recv_ms = rms;
    return 1;
}

static void tel_heartbeat(uint8_t *rg, double t)
{
    float *info = (float *)(rg + RG_INFO + HB_POS);
    fprintf(g_tel, "H,%.3f,%u,%d,%u,%.3f,%.3f,%.3f,%04x,%u\n", t, sim_ms(), FLD(rg, RG_OBJ, int), FLD(rg, RG_SEQ, uint16_t),
            info[0], info[1], info[2], FLD(rg, RG_INFO + HB_FLAGS, uint16_t), FLD(rg, RG_INFO + HB_MODE, uint8_t));
}

static void tel_steer(uint8_t *rg, uint32_t flags, double t, const float before[3], const float target[3],
                      const steer_info_t *si)
{
    int obj = FLD(rg, RG_OBJ, int);
    float *mp = model_pos(obj);
    fprintf(g_tel, "R,%.3f,%u,%d,%08x,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%.1f,%d\n", t, sim_ms(), obj,
            flags, before[0], before[1], before[2], mp ? mp[0] : 0, mp ? mp[1] : 0, mp ? mp[2] : 0, target[0],
            target[1], target[2], si->fixed, si->mode, si->delay_ms, si->fallback);
}

/* Telemetry without hooking the steer function: walk the remote ghost table around the stock ghost frame.
 * Pass 0 (before) keeps each player's position, pass 1 (after) writes the H and R lines. */
static void tel_remote_frame(int after)
{
    static struct { int obj; float pos[3]; uint32_t flags; } pre[16];
    static int n_pre;
    uint8_t *hash = VA(A_REMOTE_GHOSTS);
    uint8_t ***buckets = *(uint8_t ****)(hash + 4);
    uint32_t n = *(uint32_t *)(hash + 8);
    if (!after) n_pre = 0;
    double t = now_ms();
    for (uint32_t i = 0; buckets && i < n; i++) {
        for (uint8_t **node = buckets[i]; node; node = (uint8_t **)node[1]) {
            uint8_t *rg = node[0];
            if (!rg || (FLD(rg, RG_FLAGS, uint32_t) & GF_TYPE_MASK) != GF_TYPE_PLAYER) continue;
            int obj = FLD(rg, RG_OBJ, int);
            float *mp = model_pos(obj);
            if (!after) {
                if (n_pre < 16 && mp) {
                    pre[n_pre].obj = obj;
                    memcpy(pre[n_pre].pos, mp, 12);
                    pre[n_pre].flags = FLD(rg, RG_FLAGS, uint32_t);
                    n_pre++;
                }
                continue;
            }
            int k = 0;
            while (k < n_pre && pre[k].obj != obj) k++;
            if (k == n_pre) continue;                        /* new this frame */
            if (new_heartbeat(obj, FLD(rg, RG_SEQ, uint16_t), FLD(rg, RG_RECV_MS, uint32_t))) tel_heartbeat(rg, t);
            steer_info_t si = { 0, -1, 0, 0.0 };
            tel_steer(rg, pre[k].flags, t, pre[k].pos, (float *)(rg + RG_PRED + HB_POS), &si);
        }
    }
}

static void __cdecl hk_steer(uint8_t *rg, float dt)
{
    uint32_t flags = FLD(rg, RG_FLAGS, uint32_t);
    if ((flags & GF_TYPE_MASK) != GF_TYPE_PLAYER) { o_steer(rg, dt); return; }

    int obj = FLD(rg, RG_OBJ, int);
    double t = now_ms();
    int new_hb = new_heartbeat(obj, FLD(rg, RG_SEQ, uint16_t), FLD(rg, RG_RECV_MS, uint32_t));
    if (new_hb && g_tel) tel_heartbeat(rg, t);

    float *mp = model_pos(obj);
    float before[3] = { 0, 0, 0 };
    if (mp) memcpy(before, mp, 12);

    steer_info_t si = { 0, -1, 0, 0.0 };
    if (ghost_steer_pre) ghost_steer_pre(rg, t, new_hb, &si);
    float *pred = (float *)(rg + RG_PRED + HB_POS);
    float target[3] = { pred[0], pred[1], pred[2] };

    o_steer(rg, dt);

    if (ghost_steer_post) ghost_steer_post(rg, target, &si);
    if (g_tel) tel_steer(rg, flags, t, before, target, &si);
}

/* ---------- setup ---------- */

int sig_ok(uintptr_t va, const char *sig, size_t n)
{
    return memcmp(VA(va), sig, n) == 0;
}

int sig_rebased(uintptr_t va, const char *sig, size_t n)
{
    char buf[32];
    memcpy(buf, sig, n);
    uint32_t a = *(uint32_t *)(buf + 1) - EXE_PREF_BASE + (uint32_t)g_base;
    memcpy(buf + 1, &a, 4);
    return memcmp(VA(va), buf, n) == 0;
}

const char *sig_mismatch(uintptr_t va)
{
    static char buf[64];
    snprintf(buf, sizeof buf, "signature mismatch at %#x, left stock", (unsigned)va);
    return buf;
}

int hook(uintptr_t va, void *det, void **orig, const char *name)
{
    MH_STATUS s = MH_CreateHook(VA(va), det, orig);
    if (s == MH_OK) s = MH_EnableHook(VA(va));
    MEMORY_BASIC_INFORMATION mbi = { 0 };
    if (*orig) VirtualQuery(*orig, &mbi, sizeof mbi);
    logf_("hook %-14s %#x: %s, trampoline %p protect %#lx", name, (unsigned)va, MH_StatusToString(s), *orig,
          (unsigned long)mbi.Protect);
    return s == MH_OK;
}

int patch_bytes(uintptr_t va, const void *bytes, size_t n)
{
    DWORD old;
    if (!VirtualProtect(VA(va), n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(VA(va), bytes, n);
    VirtualProtect(VA(va), n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), VA(va), n);
    logf_("patch %#x: %u bytes", (unsigned)va, (unsigned)n);
    return 1;
}

/* A core ghost hook, if something needs it and its bytes match. */
static void ghost_hook(unsigned need, unsigned bit, uintptr_t va, const char *sig, size_t n, void *det, void **orig,
                       const char *name)
{
    if (!(need & bit)) return;
    if (!sig_ok(va, sig, n)) { logf_("hook %-14s %#x: signature mismatch, not hooked", name, (unsigned)va); return; }
    hook(va, det, orig, name);
}
#define GHOST_HOOK(need, bit, va, sig, det, orig, name) ghost_hook(need, bit, va, sig, sizeof(sig) - 1, det, (void **)orig, name)

static void load_config(const char *ini, const char *dir)
{
    cfg.telemetry = GetPrivateProfileIntA("T2MPFix", "telemetry", 0, ini);
    cfg.qa = GetPrivateProfileIntA("qa", "enable", 0, ini);
    g_hook_mask = GetPrivateProfileIntA("qa", "hooks", 31, ini);   /* bisecting aid */
    cfg.net_delay_ms = GetPrivateProfileIntA("qa", "net_delay_ms", 0, ini);
    cfg.net_jitter_ms = GetPrivateProfileIntA("qa", "net_jitter_ms", 0, ini);
    cfg.net_loss_pct = GetPrivateProfileIntA("qa", "net_loss_pct", 0, ini);
    cfg.net_seed = GetPrivateProfileIntA("qa", "net_seed", 1, ini);
    char bot[MAX_PATH] = "";
    GetPrivateProfileStringA("qa", "bot", "", bot, sizeof bot, ini);
    if (bot[0]) snprintf(cfg.bot, sizeof cfg.bot, "%s%s", dir, bot);
}

static void init(void)
{
    char dir[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    char *slash = strrchr(dir, '\\');
    if (slash) slash[1] = 0;

    HMODULE exe = GetModuleHandleA(NULL);
    g_base = (uintptr_t)exe;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((uint8_t *)exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != EXE_TIMESTAMP || nt->OptionalHeader.SizeOfImage != EXE_IMAGESIZE)
        return;   /* not the 1.29 MP exe (e.g. single-player thief2.exe): stay a plain dinput proxy */

    QueryPerformanceFrequency(&g_qpf);
    char ini[MAX_PATH], path[MAX_PATH];
    snprintf(ini, sizeof ini, "%sT2MPFix.ini", dir);
    load_config(ini, dir);
    snprintf(path, sizeof path, "%sT2MPFix.log", dir);
    g_log = fopen(path, "w");
    logf_("T2MPFix 0.3 on Thief2MP 1.29, base %p; telemetry=%d qa=%d hooks=%d", (void *)g_base, cfg.telemetry,
          cfg.qa, g_hook_mask);
    if (MH_Initialize() != MH_OK) { logf_("MH_Initialize failed"); return; }

    /* Each fix decides on its own: a missing switch or a signature mismatch only turns that fix off. */
    char active[128] = "";
    int any_tick = 0;
    for (int i = 0; i < N_FIXES; i++) {
        const char *why = fixes[i]->init(ini);
        fix_on[i] = why == NULL;
        logf_("%s: %s", fixes[i]->name, why ? why : "on");
        if (fix_on[i]) {
            strcat(active, " ");
            strcat(active, fixes[i]->name);
            any_tick |= fixes[i]->tick != NULL;
        }
    }
    logf_("active:%s", active[0] ? active : " none (stock game)");

    if (cfg.telemetry) {
        snprintf(path, sizeof path, "%sT2MPFix_tel.csv", dir);
        g_tel = fopen(path, "w");
        if (g_tel) setvbuf(g_tel, NULL, _IOFBF, 1 << 16);
    }
    bot_load();
    unsigned need = ghost_hooks_needed(ghost_send_force != NULL, ghost_steer_pre != NULL, cfg.telemetry, cfg.qa,
                                       bot_n > 0, netsim_on()) & (unsigned)g_hook_mask;
    if ((need & (HK_GHOST_FRAME | HK_SEND_DECIDE | HK_STEER)) && !SIG(A_PHYS_GET_MODEL, SIG_PHYS_GET_MODEL)) {
        logf_("%s, ghost hooks not installed", sig_mismatch(A_PHYS_GET_MODEL));
        need = 0;
    }
    GHOST_HOOK(need, HK_GHOST_FRAME, A_GHOST_FRAME, SIG_GHOST_FRAME, hk_ghost_frame, &o_ghost_frame, "ghost_frame");
    GHOST_HOOK(need, HK_SEND_DECIDE, A_SEND_DECIDE, SIG_SEND_DECIDE, hk_send_decide, &o_send_decide, "send_decide");
    GHOST_HOOK(need, HK_SEND, A_SEND, SIG_SEND, hk_send, &o_send, "send");
    GHOST_HOOK(need, HK_STEER, A_REMOTE_STEER, SIG_REMOTE_STEER, hk_steer, &o_steer, "remote_steer");
    if (ghost_send_force && !o_send_decide) { ghost_send_force = NULL; logf_("send_fix: hook failed, off"); }
    if (ghost_steer_pre && !o_steer) { ghost_steer_pre = NULL; ghost_steer_post = NULL; logf_("recv_fix: hook failed, off"); }
    if (netsim_on() && o_ghost_frame && SIG(A_RECV_HEARTBEAT, SIG_RECV_HEARTBEAT)) {
        hook(A_RECV_HEARTBEAT, hk_recv_hb, (void **)&o_recv_hb, "recv_heartbeat");
        logf_("netsim: delay %d ms, jitter +-%d ms, loss %d%%, seed %d", cfg.net_delay_ms, cfg.net_jitter_ms,
              cfg.net_loss_pct, cfg.net_seed);
    }
    if (cfg.qa || any_tick) qa_init(dir, cfg.qa, any_tick);
}

static void shutdown_(void)
{
    if (!g_log) return;
    for (int i = 0; i < N_FIXES; i++)
        if (fix_on[i] && fixes[i]->exit) fixes[i]->exit();
    logf_("exit: frames %u, netsim delayed %u dropped %u", g_frames, net_delayed, net_dropped);
    if (g_tel) fclose(g_tel);
    fclose(g_log);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); init(); }
    else if (reason == DLL_PROCESS_DETACH) shutdown_();
    return TRUE;
}
