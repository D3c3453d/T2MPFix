/* ghostfix: smooth, accurate remote player avatars for Thief 2 NewDark 1.29 multiplayer.
 *
 * Stock behaviour (see games/thief-2/NOTES.md):
 *  - the sender only sends a player heartbeat when the receiver's dead-reckoned guess is off by
 *    more than 4 units on an axis;
 *  - the receiver dead-reckons with gravity and a raycast that stops at walls, then *steers* the
 *    avatar's physics body toward that guess with a velocity (gain 2/s), colliding with the world
 *    on the way, and only teleports when the error exceeds 12 units.
 * Result: avatars that lag, stick on doorframes and stairs, float, sink, or vanish into walls.
 *
 * ghostfix (all machines must run it):
 *  - send: while a player moves or turns, send a heartbeat every send_interval_ms (default 50);
 *  - receive: buffer the received states and show the remote player on the interpolated path,
 *    a fixed small delay behind, by placing the avatar there every frame with the engine's own
 *    warp, instead of steering it through local physics.
 */
#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "MinHook.h"
#include "engine.h"
#include "interp.h"
#include "common.h"

uintptr_t g_base;

static struct {
    int send_fix, recv_fix, telemetry, send_interval_ms, qa, hooks;
    int net_delay_ms, net_jitter_ms, net_loss_pct, net_seed;   /* QA: impair incoming heartbeats */
    int max_players;
    interp_cfg_t ic;
    char bot[MAX_PATH];
} cfg;

static FILE *g_log;
FILE *g_tel;
static LARGE_INTEGER g_qpf;
static int g_local_player;          /* obj id of our player (from the local ghost table) */
static unsigned g_forced_sends, g_frames, g_fallbacks;
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

typedef uint8_t *(__stdcall *phys_get_model_t)(int obj);   /* ret 4 */
static float *model_pos(int obj)
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

static int16_t *obj_facing(int obj)
{
    uint8_t *tab = *(uint8_t **)VA(A_POSITION_TABLE);
    if (!tab || obj < 0 || (uint32_t)obj >= *(uint32_t *)(tab + 0x20)) return NULL;
    uint8_t *pos = (*(uint8_t ***)(tab + 0x18))[obj];
    return pos ? (int16_t *)(pos + 0x10) : NULL;
}

/* Unconditional placement (no "does the body fit" check), used when the engine's warp refuses. */
static void obj_pos_update(int obj, const float *pos, const int16_t *facing)
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

/* Player cap. Stock NewDark keeps 4 but a 4th joiner gets other players kicked; Figgis found the cap
 * must be set to N+1 for N players (systemshock.org topic 12190). Applied once from the menu tick. */
static void apply_max_players(void)
{
    static int done;
    if (done || cfg.max_players <= 0) return;
    void **agg = *(void ***)VA(A_APP_AGGREGATE);
    if (!agg) return;
    done = 1;
    typedef long(__stdcall * qi_t)(void *, const void *, void **);
    typedef unsigned long(__stdcall * rel_t)(void *);
    typedef void(__stdcall * setmax_t)(void *, int);
    void **nm = NULL;
    if (((qi_t)((void **)*agg)[0])(agg, VA(A_IID_NETMANAGER), (void **)&nm) != 0 || !nm) {
        logf_("max_players: net manager not found");
        return;
    }
    void **vt = (void **)*nm;
    if ((uintptr_t)vt != (uintptr_t)VA(A_NETMANAGER_VTABLE)) {
        logf_("max_players: unexpected vtable %p, not changing anything", (void *)vt);
    } else {
        int before = FLD(nm, NETMAN_MAXPLAYERS, int);
        ((setmax_t)vt[NETMAN_SLOT_SETMAX])(nm, cfg.max_players + 1);
        logf_("max_players: %d players (cap field %d -> %d)", cfg.max_players, before, FLD(nm, NETMAN_MAXPLAYERS, int));
    }
    ((rel_t)((void **)*nm)[2])(nm);
}

/* Called from the message loop hook (menus and in game). */
void game_tick(void) { apply_max_players(); }

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

/* ---------- per remote player state ---------- */

static float len3(const float *v) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

typedef struct {
    int obj;
    uint16_t seq;
    uint32_t recv_ms;
    int fast;            /* sender proven to run ghostfix (high heartbeat rate while moving) */
    interp_t buf;
} rstate_t;

static rstate_t rs[16];

/* A player without ghostfix sends ~1.3 heartbeats/s while moving, one with it ~16/s (14+ even with 10% loss
 * and heavy jitter; stock bursts on state changes but never beats its 100 ms minimum interval). Interpolating the
 * sparse stock stream would cut corners, so keep stock behaviour for that player. Decide on the rate over the
 * last 1.5 s of buffered snapshots while the player moves. */
static void classify_sender(rstate_t *st, double now)
{
    interp_t *b = &st->buf;
    if (b->count < 2) return;
    const snap_t *last = &b->s[(b->head + b->count - 1) % INTERP_SLOTS];
    if (len3(last->vel) < 1.0f) return;                     /* standing still: no evidence either way */
    int n = 0;
    double oldest = now;
    for (int i = 0; i < b->count; i++) {
        const snap_t *x = &b->s[(b->head + i) % INTERP_SLOTS];
        if (x->t_ms >= now - 1500) n++;
        if (x->t_ms < oldest) oldest = x->t_ms;
    }
    if (n >= 18) st->fast = 1;   /* >= 12/s: stock can't reach this (100 ms minimum interval for players) */
    else if (oldest <= now - 1500 && n <= 4) st->fast = 0;    /* <= 2.7/s with 1.5 s of history */
}

static rstate_t *rstate(int obj)
{
    rstate_t *free_ = NULL;
    for (int i = 0; i < 16; i++) {
        if (rs[i].obj == obj) return &rs[i];
        if (!rs[i].obj && !free_) free_ = &rs[i];
    }
    if (!free_) free_ = &rs[(unsigned)obj % 16];
    memset(free_, 0, sizeof *free_);
    free_->obj = obj;
    free_->seq = 0xffff;
    free_->recv_ms = 0xffffffff;
    return free_;
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

static uint32_t rng(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }

static void __cdecl hk_recv_hb(int obj, int seq, int relobj, uint8_t *hb, int16_t *extra)
{
    if (!cfg.net_delay_ms && !cfg.net_jitter_ms && !cfg.net_loss_pct) { o_recv_hb(obj, seq, relobj, hb, extra); return; }
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

/* ---------- hooks ---------- */

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
    o_ghost_frame(dt_ms);
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
    if (r || !cfg.send_fix) return r;

    float *mp = model_pos(obj);
    if (!mp || memcmp(hb + HB_POS, mp, 12) != 0) return r;   /* heartbeat not filled in */
    if (sim_ms() - FLD(lg, LG_LAST_SEND_MS, uint32_t) < (uint32_t)cfg.send_interval_ms) return r;

    float *pos = (float *)(hb + HB_POS), *vel = (float *)(hb + HB_VEL);
    float *spos = (float *)(lg + LG_SENT + HB_POS), *svel = (float *)(lg + LG_SENT + HB_VEL);
    float d[3] = { pos[0] - spos[0], pos[1] - spos[1], pos[2] - spos[2] };
    int moved = len3(d) > 0.02f;
    int moving = len3(vel) > 0.05f || len3(svel) > 0.05f;      /* includes the final "stopped" */
    int turned = FLD(hb, HB_HEADING, uint16_t) != FLD(lg, LG_SENT + HB_HEADING, uint16_t);
    if (moved || moving || turned) { g_forced_sends++; return 1; }
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

static void __cdecl hk_steer(uint8_t *rg, float dt)
{
    uint32_t flags = FLD(rg, RG_FLAGS, uint32_t);
    if ((flags & GF_TYPE_MASK) != GF_TYPE_PLAYER) { o_steer(rg, dt); return; }

    int obj = FLD(rg, RG_OBJ, int);
    rstate_t *st = rstate(obj);
    double t = now_ms();
    uint16_t seq = FLD(rg, RG_SEQ, uint16_t);
    uint32_t rms = FLD(rg, RG_RECV_MS, uint32_t);
    float *info = (float *)(rg + RG_INFO + HB_POS);

    if (seq != st->seq || rms != st->recv_ms) {
        snap_t s;
        s.t_ms = t;
        memcpy(s.pos, info, 12);
        memcpy(s.vel, rg + RG_INFO + HB_VEL, 12);
        s.heading = FLD(rg, RG_INFO + HB_HEADING, uint16_t);
        s.relobj = FLD(rg, RG_RELOBJ, int32_t);
        interp_push(&st->buf, &cfg.ic, &s);
        classify_sender(st, t);
        st->seq = seq;
        st->recv_ms = rms;
        if (g_tel)
            fprintf(g_tel, "H,%.3f,%u,%d,%u,%.3f,%.3f,%.3f,%04x,%u\n", t, sim_ms(), obj, seq, info[0], info[1], info[2],
                    FLD(rg, RG_INFO + HB_FLAGS, uint16_t), FLD(rg, RG_INFO + HB_MODE, uint8_t));
    }

    float *mp = model_pos(obj);
    float before[3] = { 0, 0, 0 };
    if (mp) memcpy(before, mp, 12);

    interp_out_t o;
    int fixed = 0;
    /* A player without ghostfix sends ~1/s; interpolating that would cut corners, so keep stock for them. */
    if (cfg.recv_fix && st->fast && interp_sample(&st->buf, &cfg.ic, t, &o) &&
        o.relobj == FLD(rg, RG_RELOBJ, int32_t)) {
        memcpy(rg + RG_PRED + HB_POS, o.pos, 12);
        memcpy(rg + RG_PRED + HB_VEL, o.vel, 12);
        FLD(rg, RG_PRED + HB_HEADING, uint16_t) = o.heading;
        FLD(rg, RG_FLAGS, uint32_t) |= RGF_FORCE_WARP;          /* stock code warps to pred */
        fixed = 1;
    }
    float *pred = (float *)(rg + RG_PRED + HB_POS);
    float target[3] = { pred[0], pred[1], pred[2] };

    o_steer(rg, dt);

    int fallback = 0;
    mp = model_pos(obj);
    if (fixed && mp && FLD(rg, RG_RELOBJ, int32_t) == 0) {
        float d[3] = { mp[0] - target[0], mp[1] - target[1], mp[2] - target[2] };
        int16_t *fac = obj_facing(obj);
        if (len3(d) > 0.5f && fac) {
            int16_t keep[3] = { fac[0], fac[1], fac[2] };
            obj_pos_update(obj, target, keep);
            fallback = 1;
            g_fallbacks++;
        }
    }

    if (g_tel) {
        fprintf(g_tel, "R,%.3f,%u,%d,%08x,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%.1f,%d\n", t, sim_ms(), obj,
                flags, before[0], before[1], before[2], mp ? mp[0] : 0, mp ? mp[1] : 0, mp ? mp[2] : 0, target[0],
                target[1], target[2], fixed, fixed ? o.mode : -1, fixed ? o.delay_ms : 0.0, fallback);
    }
}

/* ---------- setup ---------- */

static int sig_ok(uintptr_t va, const char *sig, size_t n)
{
    return memcmp(VA(va), sig, n) == 0;
}
#define SIG(va, s) sig_ok(va, s, sizeof(s) - 1)

/* Signature whose bytes 1..4 are an absolute address (relocated by the loader). */
static int sig_rebased(uintptr_t va, const char *sig, size_t n)
{
    char buf[32];
    memcpy(buf, sig, n);
    uint32_t a = *(uint32_t *)(buf + 1) - EXE_PREF_BASE + (uint32_t)g_base;
    memcpy(buf + 1, &a, 4);
    return memcmp(VA(va), buf, n) == 0;
}

static void load_config(const char *dir)
{
    char ini[MAX_PATH];
    snprintf(ini, sizeof ini, "%sghostfix.ini", dir);
    cfg.send_fix = GetPrivateProfileIntA("ghostfix", "send_fix", 1, ini);
    cfg.recv_fix = GetPrivateProfileIntA("ghostfix", "recv_fix", 1, ini);
    cfg.telemetry = GetPrivateProfileIntA("ghostfix", "telemetry", 0, ini);
    cfg.send_interval_ms = GetPrivateProfileIntA("ghostfix", "send_interval_ms", 50, ini);
    cfg.ic.delay_min_ms = GetPrivateProfileIntA("ghostfix", "delay_min_ms", 60, ini);
    cfg.ic.delay_max_ms = GetPrivateProfileIntA("ghostfix", "delay_max_ms", 300, ini);
    cfg.ic.nominal_gap_ms = cfg.send_interval_ms;
    cfg.ic.max_extrap_ms = GetPrivateProfileIntA("ghostfix", "max_extrap_ms", 100, ini);
    cfg.ic.snap_dist = (float)GetPrivateProfileIntA("ghostfix", "snap_dist", 20, ini);
    cfg.max_players = GetPrivateProfileIntA("ghostfix", "max_players", 0, ini);
    cfg.qa = GetPrivateProfileIntA("qa", "enable", 0, ini);
    cfg.hooks = GetPrivateProfileIntA("qa", "hooks", 15, ini);   /* bisecting aid */
    cfg.net_delay_ms = GetPrivateProfileIntA("qa", "net_delay_ms", 0, ini);
    cfg.net_jitter_ms = GetPrivateProfileIntA("qa", "net_jitter_ms", 0, ini);
    cfg.net_loss_pct = GetPrivateProfileIntA("qa", "net_loss_pct", 0, ini);
    cfg.net_seed = GetPrivateProfileIntA("qa", "net_seed", 1, ini);
    char bot[MAX_PATH] = "";
    GetPrivateProfileStringA("qa", "bot", "", bot, sizeof bot, ini);
    if (bot[0]) snprintf(cfg.bot, sizeof cfg.bot, "%s%s", dir, bot);
}

static int hook(uintptr_t va, void *det, void **orig, const char *name)
{
    MH_STATUS s = MH_CreateHook(VA(va), det, orig);
    if (s == MH_OK) s = MH_EnableHook(VA(va));
    MEMORY_BASIC_INFORMATION mbi = { 0 };
    if (*orig) VirtualQuery(*orig, &mbi, sizeof mbi);
    logf_("hook %-14s %#x: %s, trampoline %p protect %#lx", name, (unsigned)va, MH_StatusToString(s), *orig,
          (unsigned long)mbi.Protect);
    return s == MH_OK;
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
    load_config(dir);
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%sghostfix.log", dir);
    g_log = fopen(path, "w");
    logf_("ghostfix 0.1 on Thief2MP 1.29, base %p; send_fix=%d recv_fix=%d interval=%d telemetry=%d",
          (void *)g_base, cfg.send_fix, cfg.recv_fix, cfg.send_interval_ms, cfg.telemetry);

    if (!SIG(A_GHOST_FRAME, SIG_GHOST_FRAME) || !SIG(A_SEND_DECIDE, SIG_SEND_DECIDE) || !SIG(A_SEND, SIG_SEND) ||
        !SIG(A_REMOTE_STEER, SIG_REMOTE_STEER) || !SIG(A_PHYS_GET_MODEL, SIG_PHYS_GET_MODEL) ||
        !sig_rebased(A_OBJ_POS_UPDATE, SIG_OBJ_POS_UPDATE, sizeof(SIG_OBJ_POS_UPDATE) - 1)) {
        logf_("signature mismatch: not hooking anything");
        return;
    }
    if (cfg.telemetry) {
        snprintf(path, sizeof path, "%sghostfix_tel.csv", dir);
        g_tel = fopen(path, "w");
        if (g_tel) setvbuf(g_tel, NULL, _IOFBF, 1 << 16);
    }
    bot_load();
    if (MH_Initialize() != MH_OK) { logf_("MH_Initialize failed"); return; }
    if (cfg.hooks & 1) hook(A_GHOST_FRAME, hk_ghost_frame, (void **)&o_ghost_frame, "ghost_frame");
    if (cfg.hooks & 2) hook(A_SEND_DECIDE, hk_send_decide, (void **)&o_send_decide, "send_decide");
    if (cfg.hooks & 4) hook(A_SEND, hk_send, (void **)&o_send, "send");
    if (cfg.hooks & 8) hook(A_REMOTE_STEER, hk_steer, (void **)&o_steer, "remote_steer");
    if ((cfg.net_delay_ms || cfg.net_jitter_ms || cfg.net_loss_pct) && SIG(A_RECV_HEARTBEAT, SIG_RECV_HEARTBEAT)) {
        hook(A_RECV_HEARTBEAT, hk_recv_hb, (void **)&o_recv_hb, "recv_heartbeat");
        logf_("netsim: delay %d ms, jitter +-%d ms, loss %d%%, seed %d", cfg.net_delay_ms, cfg.net_jitter_ms,
              cfg.net_loss_pct, cfg.net_seed);
    }
    qa_init(dir, cfg.qa);
}

static void shutdown_(void)
{
    logf_("exit: frames %u, forced sends %u, placement fallbacks %u, netsim delayed %u dropped %u", g_frames,
          g_forced_sends, g_fallbacks, net_delayed, net_dropped);
    if (g_tel) fclose(g_tel);
    if (g_log) fclose(g_log);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); init(); }
    else if (reason == DLL_PROCESS_DETACH) shutdown_();
    return TRUE;
}
