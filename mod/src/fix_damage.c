/* damage_fix: damage from local-only objects (lava) is applied again; the rule and why are in damage.h. */
#include <windows.h>
#include "MinHook.h"
#include "engine.h"
#include "damage.h"
#include "common.h"
#include "fixes.h"

typedef int(__stdcall *damage_filter_t)(int victim, int culprit, void *msg, void *data);
typedef int(__stdcall *objnet_pred_t)(void *self, int obj);
static damage_filter_t o_damage_filter;
static unsigned applied;

static int objnet(void *on, int slot, int obj) { return ((objnet_pred_t)(*(void ***)on)[slot / 4])(on, obj); }

/* The engine zeroes damage from local-only culprits (lava) for every victim; let it through for victims
 * this machine hosts, and leave everything else to the engine. */
static int __stdcall hk_damage_filter(int victim, int culprit, void *msg, void *data)
{
    void *on = *(void **)VA(A_OBJ_NETWORKING);
    if (on && culprit && victim &&
        local_damage_allowed(objnet(on, OBJNET_LOCAL_ONLY, culprit), objnet(on, OBJNET_IS_PROXY, victim),
                             objnet(on, OBJNET_HOSTED_HERE, victim))) {
        if (++applied <= 3) logf_("damage_fix: local-only damage to obj %d from obj %d applied", victim, culprit);
        return 0;
    }
    return o_damage_filter(victim, culprit, msg, data);
}

static const char *init(const char *ini)
{
    if (!GetPrivateProfileIntA("T2MPFix", "damage_fix", 1, ini)) return "off in T2MPFix.ini";
    if (!(g_hook_mask & HK_DAMAGE)) return "off ([qa] hooks)";
    if (!SIG(A_NET_DAMAGE_FILTER, SIG_NET_DAMAGE_FILTER)) return sig_mismatch(A_NET_DAMAGE_FILTER);
    if (!hook(A_NET_DAMAGE_FILTER, hk_damage_filter, (void **)&o_damage_filter, "damage_filter"))
        return "hook failed";
    return NULL;
}

static void exit_(void) { logf_("damage_fix: local-only damage applied %u times", applied); }

const fix_t fix_damage = { "damage_fix", init, NULL, exit_ };
