/* The fixes are independent modules (fix_*.c), each switched by its own key in T2MPFix.ini:
 *   send_fix     send your position ~20x/s while moving          (fix_send.c)  } smooth remote players
 *   recv_fix     show other players on their interpolated path   (fix_recv.c)  }
 *   steer_fix    stock steering without its uninitialized read   (fix_steer.c)  players not interpolated
 *   max_players  player cap N+1, so N players can join           (fix_players.c)
 *   damage_fix   local-only hazards (e.g. lava) hurt again       (fix_damage.c)
 * A fix that is off (or whose engine code doesn't match) installs nothing and leaves the engine stock.
 * To retire one for good: delete its file, its line in main.c's fixes[], and its entry in the build lists. */
#pragma once
#include <stdint.h>

typedef struct {
    const char *name;                     /* its T2MPFix.ini key */
    const char *(*init)(const char *ini); /* read own keys, check own signatures, hook; NULL = on, else why off */
    void (*tick)(void);                   /* optional: game thread, every message-loop pass (menus too) */
    void (*exit)(void);                   /* optional: log own counters */
} fix_t;

extern const fix_t fix_send, fix_recv, fix_steer, fix_players, fix_damage;

/* Extension points in main.c's ghost hooks. NULL = stock. send_fix and recv_fix set them; main.c owns
 * the hooks because QA needs the send decision (local player) with every fix off too. */
typedef struct { int fixed, mode, fallback; double delay_ms; } steer_info_t;
extern int (*ghost_send_force)(uint8_t *lg, uint8_t *hb);   /* stock said no: 1 = send anyway */
extern void (*ghost_steer_pre)(uint8_t *rg, double now, int new_hb, steer_info_t *si);  /* may rewrite RG pred */
extern void (*ghost_steer_post)(uint8_t *rg, const float target[3], steer_info_t *si);

/* [qa] hooks bits (bisecting aid). */
#define HK_GHOST_FRAME  1
#define HK_SEND_DECIDE  2
#define HK_SEND         4
#define HK_STEER        8
#define HK_DAMAGE       16

/* Which of main.c's ghost hooks are needed (unit tested in tests/test_fixes.c). The remote steer function is
 * hooked only for recv_fix: it reads uninitialized stack (see fix_steer.c), so a hook in its call chain changes
 * how stock steering behaves. Telemetry reads remote players from the ghost frame instead. */
static inline unsigned ghost_hooks_needed(int send_fix, int recv_fix, int telemetry, int qa, int bot, int netsim)
{
    unsigned m = 0;
    if (telemetry || bot || netsim) m |= HK_GHOST_FRAME;          /* L, H, R lines; bot steps; netsim delivery */
    if (send_fix || telemetry || qa || bot) m |= HK_SEND_DECIDE;  /* the fix; local player for QA and bot */
    if (telemetry) m |= HK_SEND;                                  /* S lines */
    if (recv_fix) m |= HK_STEER;                                  /* the fix */
    return m;
}
