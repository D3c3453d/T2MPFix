/* Damage from local-only objects (lava and other Water Flow Groups) in multiplayer.
 *
 * Thief2MP's damage filter (A_NET_DAMAGE_FILTER) zeroes any damage whose culprit is neither hosted
 * here nor a proxy, which is every Local Only object. Lava's FireStim reaches the player, the damage
 * receptron runs, and the filter turns the damage into 0 on every machine, so lava never hurts anyone.
 * Single player never reaches that check. The fix applies such damage on the machine that hosts the
 * victim: each player takes it from their own local copy of the lava, and proxies (other players'
 * avatars here) still go through the engine, which blocks them, so nothing is applied twice. */
#pragma once

static inline int local_damage_allowed(int culprit_local_only, int victim_proxy, int victim_hosted_here)
{
    return culprit_local_only && !victim_proxy && victim_hosted_here;
}
