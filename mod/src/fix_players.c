/* max_players: the host's player cap. Stock NewDark keeps 4, but a 4th joiner gets two other players kicked;
 * Figgis found the cap must be set to N+1 for N players (systemshock.org topic 12190). Applied once, from the
 * menu loop, as soon as the net manager exists. */
#include <windows.h>
#include "engine.h"
#include "common.h"
#include "fixes.h"

static int max_players;

static void tick(void)
{
    static int done;
    if (done) return;
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
        ((setmax_t)vt[NETMAN_SLOT_SETMAX])(nm, max_players + 1);
        logf_("max_players: %d players (cap field %d -> %d)", max_players, before, FLD(nm, NETMAN_MAXPLAYERS, int));
    }
    ((rel_t)((void **)*nm)[2])(nm);
}

static const char *init(const char *ini)
{
    max_players = GetPrivateProfileIntA("T2MPFix", "max_players", 0, ini);
    if (max_players <= 0) return "off in T2MPFix.ini (stock cap)";
    return NULL;
}

const fix_t fix_players = { "max_players", init, tick, NULL };
