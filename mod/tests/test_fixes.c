/* Host-side tests for fixes.h: which ghost hooks get installed. gcc -O1 -I../src test_fixes.c && ./a.out */
#include "fixes.h"
#include <stdio.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define NEED(want, ...) do { unsigned got = ghost_hooks_needed(__VA_ARGS__); \
    CHECK(got == (want), "ghost_hooks_needed(%s) = %#x, want %#x", #__VA_ARGS__, got, (unsigned)(want)); } while (0)

int main(void)
{
    /*   want                                     send recv tel  qa  bot netsim */
    /* Every fix off, no QA: nothing in the ghost code is touched. */
    NEED(0,                                         0,   0,   0,  0,  0,  0);
    /* Each avatar half hooks only its own function. */
    NEED(HK_SEND_DECIDE,                            1,   0,   0,  0,  0,  0);
    NEED(HK_STEER,                                  0,   1,   0,  0,  0,  0);
    NEED(HK_SEND_DECIDE | HK_STEER,                 1,   1,   0,  0,  0,  0);
    /* Telemetry never hooks the steer function (stock steering depends on the stack in its call chain). */
    NEED(HK_GHOST_FRAME | HK_SEND_DECIDE | HK_SEND, 0, 0, 1, 0, 0, 0);
    NEED(HK_GHOST_FRAME | HK_SEND_DECIDE | HK_SEND | HK_STEER, 1, 1, 1, 0, 0, 0);
    NEED(HK_GHOST_FRAME | HK_SEND_DECIDE | HK_SEND, 1, 0, 1, 1, 1, 1);
    /* The QA channel reports the local player; the bot also runs from the ghost frame. */
    NEED(HK_SEND_DECIDE,                            0,   0,   0,  1,  0,  0);
    NEED(HK_GHOST_FRAME | HK_SEND_DECIDE,           0,   0,   0,  1,  1,  0);
    /* Network impairment delivers delayed heartbeats from the ghost frame. */
    NEED(HK_GHOST_FRAME,                            0,   0,   0,  0,  0,  1);
    /* The damage hook is never a ghost hook (fix_damage.c installs it). */
    for (int m = 0; m < 64; m++)
        CHECK(!(ghost_hooks_needed(m & 1, m & 2, m & 4, m & 8, m & 16, m & 32) & HK_DAMAGE), "damage bit, case %d", m);
    printf(fails ? "fixes: %d FAILED\n" : "fixes: all passed\n", fails);
    return fails != 0;
}
