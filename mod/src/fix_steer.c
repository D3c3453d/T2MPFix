/* steer_fix: the stock steering of a remote player reads three stack floats it never wrote.
 *
 * For a moving player, the stock steer code (A_REMOTE_STEER) picks the avatar's physics velocity with
 *     if (|v|^2 <= 1) vel = pred_vel;  else vel = (pred - pos) + pred_vel;
 * but v is three locals that are not written on this path: whatever the previous call at that stack depth left.
 * With small leftovers the avatar only follows the predicted velocity and drifts behind until the 12-unit snap;
 * with large ones it is pulled toward the prediction. Which one you get depends on the build and on any hook in
 * the call chain. This affects every remote player that recv_fix doesn't interpolate (players without send_fix,
 * or recv_fix off), and it is independent of the other fixes.
 *
 * The patch removes the branch, so a moving player is always pulled toward the prediction (vel = d + pred_vel).
 * Reading d instead of v ("correct only when more than 1 unit off") was measured too and was worse (see NOTES).
 * The bytes are checked against SIG_STEER_VEL_CHECK first; anything else leaves the engine stock. */
#include <windows.h>
#include "engine.h"
#include "common.h"
#include "fixes.h"

static const char *init(const char *ini)
{
    if (!GetPrivateProfileIntA("T2MPFix", "steer_fix", 1, ini)) return "off in T2MPFix.ini";
    if (!SIG(A_STEER_VEL_CHECK, SIG_STEER_VEL_CHECK)) return sig_mismatch(A_STEER_VEL_CHECK);
    /* jp 0x553fbd ("follow the predicted velocity only") -> 6-byte nop */
    if (!patch_bytes(A_STEER_VEL_CHECK + 46, "\x66\x0f\x1f\x44\x00\x00", 6)) return "patch failed";
    return NULL;
}

const fix_t fix_steer = { "steer_fix", init, NULL, NULL };
