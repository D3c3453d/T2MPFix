/* recv_fix: show each remote player on their real path, a small adaptive delay behind.
 *
 * Stock dead-reckons with gravity and a raycast, then steers the avatar's physics body toward that guess with
 * a velocity, colliding with the world on the way, and only teleports past 12 units: avatars stick on door
 * frames, float, sink or lag. This half buffers the received states (interp.c), writes the interpolated state
 * into the ghost's prediction and sets the force-warp flag, so the stock steer code places the avatar there.
 * It only acts on players whose heartbeat rate proves they run send_fix; others keep stock steering. */
#include <windows.h>
#include <string.h>
#include "engine.h"
#include "interp.h"
#include "common.h"
#include "fixes.h"

static interp_cfg_t ic;
static unsigned fallbacks;

typedef struct {
    int obj;
    int fast;            /* sender proven to run send_fix (high heartbeat rate while moving) */
    interp_t buf;
} rstate_t;

static rstate_t rs[16];

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
    return free_;
}

/* A player without send_fix sends ~1.3 heartbeats/s while moving, one with it ~16/s (14+ even with 10% loss
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

static void pre(uint8_t *rg, double now, int new_hb, steer_info_t *si)
{
    rstate_t *st = rstate(FLD(rg, RG_OBJ, int));
    if (new_hb) {
        snap_t s;
        s.t_ms = now;
        memcpy(s.pos, rg + RG_INFO + HB_POS, 12);
        memcpy(s.vel, rg + RG_INFO + HB_VEL, 12);
        s.heading = FLD(rg, RG_INFO + HB_HEADING, uint16_t);
        s.relobj = FLD(rg, RG_RELOBJ, int32_t);
        interp_push(&st->buf, &ic, &s);
        classify_sender(st, now);
    }
    interp_out_t o;
    if (st->fast && interp_sample(&st->buf, &ic, now, &o) && o.relobj == FLD(rg, RG_RELOBJ, int32_t)) {
        memcpy(rg + RG_PRED + HB_POS, o.pos, 12);
        memcpy(rg + RG_PRED + HB_VEL, o.vel, 12);
        FLD(rg, RG_PRED + HB_HEADING, uint16_t) = o.heading;
        FLD(rg, RG_FLAGS, uint32_t) |= RGF_FORCE_WARP;          /* stock code warps to pred */
        si->fixed = 1;
        si->mode = o.mode;
        si->delay_ms = o.delay_ms;
    }
}

/* If the engine's warp refused (the body doesn't fit), place the avatar unconditionally. */
static void post(uint8_t *rg, const float target[3], steer_info_t *si)
{
    int obj = FLD(rg, RG_OBJ, int);
    float *mp = model_pos(obj);
    if (!si->fixed || !mp || FLD(rg, RG_RELOBJ, int32_t) != 0) return;
    float d[3] = { mp[0] - target[0], mp[1] - target[1], mp[2] - target[2] };
    int16_t *fac = obj_facing(obj);
    if (len3(d) > 0.5f && fac) {
        int16_t keep[3] = { fac[0], fac[1], fac[2] };
        obj_pos_update(obj, target, keep);
        si->fallback = 1;
        fallbacks++;
    }
}

static const char *init(const char *ini)
{
    if (!GetPrivateProfileIntA("T2MPFix", "recv_fix", 1, ini)) return "off in T2MPFix.ini";
    if (!(g_hook_mask & HK_STEER)) return "off ([qa] hooks)";
    if (!SIG(A_REMOTE_STEER, SIG_REMOTE_STEER)) return sig_mismatch(A_REMOTE_STEER);
    if (!SIG(A_PHYS_GET_MODEL, SIG_PHYS_GET_MODEL)) return sig_mismatch(A_PHYS_GET_MODEL);
    if (!sig_rebased(A_OBJ_POS_UPDATE, SIG_OBJ_POS_UPDATE, sizeof(SIG_OBJ_POS_UPDATE) - 1))
        return sig_mismatch(A_OBJ_POS_UPDATE);
    ic.delay_min_ms = GetPrivateProfileIntA("T2MPFix", "delay_min_ms", 60, ini);
    ic.delay_max_ms = GetPrivateProfileIntA("T2MPFix", "delay_max_ms", 300, ini);
    ic.nominal_gap_ms = GetPrivateProfileIntA("T2MPFix", "send_interval_ms", 50, ini);   /* the senders' interval */
    ic.max_extrap_ms = GetPrivateProfileIntA("T2MPFix", "max_extrap_ms", 100, ini);
    ic.snap_dist = (float)GetPrivateProfileIntA("T2MPFix", "snap_dist", 20, ini);
    ghost_steer_pre = pre;
    ghost_steer_post = post;
    logf_("recv_fix: delay %.0f..%.0f ms, extrapolate <= %.0f ms, snap %.0f", ic.delay_min_ms, ic.delay_max_ms,
          ic.max_extrap_ms, ic.snap_dist);
    return NULL;
}

static void exit_(void) { logf_("recv_fix: placement fallbacks %u", fallbacks); }

const fix_t fix_recv = { "recv_fix", init, NULL, exit_ };
