#include "interp.h"
#include <math.h>
#include <string.h>

static snap_t *at(interp_t *b, int i) { return &b->s[(b->head + i) % INTERP_SLOTS]; }
static const snap_t *cat(const interp_t *b, int i) { return &b->s[(b->head + i) % INTERP_SLOTS]; }

void interp_reset(interp_t *b) { memset(b, 0, sizeof *b); }

static float dist3(const float *a, const float *b)
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

void interp_push(interp_t *b, const interp_cfg_t *c, const snap_t *s)
{
    if (b->count > 0) {
        snap_t *last = at(b, b->count - 1);
        if (s->relobj != last->relobj || dist3(s->pos, last->pos) > c->snap_dist) {
            interp_reset(b);                       /* teleport, respawn, platform change */
        } else if (s->t_ms <= last->t_ms) {
            *last = *s;                            /* same frame: keep the newest */
            return;
        } else {
            double gap = s->t_ms - last->t_ms;
            /* After an idle gap, the old snapshot is where the player stood until they started
             * moving again, not a point on a slow straight line from it. Re-time it so the
             * avatar starts moving about one nominal interval before the new snapshot. */
            if (gap > 3 * c->nominal_gap_ms) {
                snap_t hold = *last;
                hold.t_ms = s->t_ms - c->nominal_gap_ms;
                memset(hold.vel, 0, sizeof hold.vel);
                if (hold.t_ms > last->t_ms) {
                    if (b->count == INTERP_SLOTS) { b->head = (b->head + 1) % INTERP_SLOTS; b->count--; }
                    *at(b, b->count++) = hold;
                }
                gap = c->nominal_gap_ms;
            } else {
                if (b->avg_gap_ms == 0) b->avg_gap_ms = gap;
                double dev = fabs(gap - b->avg_gap_ms);
                b->avg_gap_ms += (gap - b->avg_gap_ms) * 0.1;
                b->jitter_ms += (dev - b->jitter_ms) * 0.1;
            }
        }
    }
    if (b->count == INTERP_SLOTS) { b->head = (b->head + 1) % INTERP_SLOTS; b->count--; }
    *at(b, b->count++) = *s;
}

double interp_delay(const interp_t *b, const interp_cfg_t *c)
{
    double gap = b->avg_gap_ms > 0 ? b->avg_gap_ms : c->nominal_gap_ms;
    double d = gap * 1.5 + b->jitter_ms * 2.0;
    if (d < c->delay_min_ms) d = c->delay_min_ms;
    if (d > c->delay_max_ms) d = c->delay_max_ms;
    return d;
}

static uint16_t lerp_heading(uint16_t a, uint16_t b, float t)
{
    int16_t d = (int16_t)(uint16_t)(b - a);        /* shortest arc */
    return (uint16_t)(a + (int)lrintf(d * t));
}

int interp_sample(const interp_t *b, const interp_cfg_t *c, double now_ms, interp_out_t *o)
{
    if (b->count == 0) return 0;
    double delay = interp_delay(b, c);
    double rt = now_ms - delay;
    const snap_t *first = cat(b, 0), *last = cat(b, b->count - 1);
    o->delay_ms = delay;
    o->relobj = last->relobj;

    if (b->count == 1 || rt <= first->t_ms) {
        const snap_t *s = b->count == 1 ? last : first;
        memcpy(o->pos, s->pos, sizeof o->pos);
        memcpy(o->vel, s->vel, sizeof o->vel);
        o->heading = s->heading;
        o->mode = 3;
        return 1;
    }
    if (rt >= last->t_ms) {
        double ex = rt - last->t_ms;
        o->mode = 1;
        if (ex > c->max_extrap_ms) { ex = c->max_extrap_ms; o->mode = 2; }
        for (int k = 0; k < 3; k++) {
            /* No gravity here: falling players send at full rate, and a gravity term is what
             * sinks the stock prediction into floors. */
            o->pos[k] = last->pos[k] + last->vel[k] * (float)(ex / 1000.0);
            o->vel[k] = o->mode == 2 ? 0.0f : last->vel[k];
        }
        o->heading = last->heading;
        return 1;
    }
    for (int i = b->count - 2; i >= 0; i--) {
        const snap_t *a = cat(b, i), *n = cat(b, i + 1);
        if (a->t_ms <= rt) {
            float t = (float)((rt - a->t_ms) / (n->t_ms - a->t_ms));
            for (int k = 0; k < 3; k++) {
                o->pos[k] = a->pos[k] + (n->pos[k] - a->pos[k]) * t;
                o->vel[k] = a->vel[k] + (n->vel[k] - a->vel[k]) * t;
            }
            o->heading = lerp_heading(a->heading, n->heading, t);
            o->mode = 0;
            return 1;
        }
    }
    return 0; /* unreachable: rt is between first and last */
}
