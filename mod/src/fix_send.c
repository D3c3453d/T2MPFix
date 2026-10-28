/* send_fix: while the local player moves or turns, send a heartbeat every send_interval_ms (default 50).
 *
 * Stock sends a player heartbeat only when the receiver's dead-reckoned guess would be more than 4 units off
 * on an axis (about 1.3/s while moving). This half alone makes the others' stock code steer your avatar from
 * fresh data; with recv_fix on the receiving side it is what lets them interpolate your real path. */
#include <windows.h>
#include <string.h>
#include "engine.h"
#include "common.h"
#include "fixes.h"

static int interval_ms;
static unsigned forced;

static int force(uint8_t *lg, uint8_t *hb)
{
    int obj = FLD(lg, LG_OBJ, int);
    float *mp = model_pos(obj);
    if (!mp || memcmp(hb + HB_POS, mp, 12) != 0) return 0;   /* heartbeat not filled in */
    if (sim_ms() - FLD(lg, LG_LAST_SEND_MS, uint32_t) < (uint32_t)interval_ms) return 0;

    float *pos = (float *)(hb + HB_POS), *vel = (float *)(hb + HB_VEL);
    float *spos = (float *)(lg + LG_SENT + HB_POS), *svel = (float *)(lg + LG_SENT + HB_VEL);
    float d[3] = { pos[0] - spos[0], pos[1] - spos[1], pos[2] - spos[2] };
    int moved = len3(d) > 0.02f;
    int moving = len3(vel) > 0.05f || len3(svel) > 0.05f;      /* includes the final "stopped" */
    int turned = FLD(hb, HB_HEADING, uint16_t) != FLD(lg, LG_SENT + HB_HEADING, uint16_t);
    if (moved || moving || turned) { forced++; return 1; }
    return 0;
}

static const char *init(const char *ini)
{
    if (!GetPrivateProfileIntA("T2MPFix", "send_fix", 1, ini)) return "off in T2MPFix.ini";
    if (!(g_hook_mask & HK_SEND_DECIDE)) return "off ([qa] hooks)";
    if (!SIG(A_SEND_DECIDE, SIG_SEND_DECIDE)) return sig_mismatch(A_SEND_DECIDE);
    if (!SIG(A_PHYS_GET_MODEL, SIG_PHYS_GET_MODEL)) return sig_mismatch(A_PHYS_GET_MODEL);
    interval_ms = GetPrivateProfileIntA("T2MPFix", "send_interval_ms", 50, ini);
    ghost_send_force = force;
    logf_("send_fix: interval %d ms", interval_ms);
    return NULL;
}

static void exit_(void) { logf_("send_fix: forced sends %u", forced); }

const fix_t fix_send = { "send_fix", init, NULL, exit_ };
