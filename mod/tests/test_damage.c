/* Host-side tests for damage.h: gcc -O1 -I../src test_damage.c && ./a.out */
#include "damage.h"
#include <stdio.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    /* Lava (a local-only Water Flow Group) hurting the player this machine owns: apply here. */
    CHECK(local_damage_allowed(1, 0, 1), "local-only culprit, own player");
    /* Same lava on this machine touching another player's avatar (a proxy): its owner applies it. */
    CHECK(!local_damage_allowed(1, 1, 0), "local-only culprit, proxy victim");
    /* Networked culprits (the ladle is Hosted) keep the engine's routing. */
    CHECK(!local_damage_allowed(0, 0, 1), "hosted culprit");
    CHECK(!local_damage_allowed(0, 1, 0), "hosted culprit, proxy victim");
    /* A victim that is neither ours nor a proxy (local-only itself) is left to the engine, which allows it. */
    CHECK(!local_damage_allowed(1, 0, 0), "victim not hosted here");
    printf(fails ? "damage: %d FAILED\n" : "damage: all passed\n", fails);
    return fails != 0;
}
