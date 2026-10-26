/* Snapshot interpolation for remote avatars. Pure C, no Windows or engine dependencies,
 * so it is unit-tested on the host (tests/test_interp.c). */
#pragma once
#include <stdint.h>

#define INTERP_SLOTS 32

typedef struct {
    double   t_ms;        /* local receive time */
    float    pos[3];      /* relative to relobj */
    float    vel[3];
    uint16_t heading;
    int32_t  relobj;
} snap_t;

typedef struct {
    snap_t   s[INTERP_SLOTS];
    int      head, count;     /* ring: s[(head + i) % SLOTS], oldest first */
    double   avg_gap_ms;      /* smoothed interval between snapshots */
    double   jitter_ms;       /* smoothed |gap - avg_gap| */
} interp_t;

typedef struct {
    double delay_min_ms;      /* lower bound of render delay */
    double delay_max_ms;      /* upper bound */
    double nominal_gap_ms;    /* expected send interval while moving */
    double max_extrap_ms;     /* extrapolate at most this far past the newest snapshot */
    float  snap_dist;         /* a jump larger than this is a teleport: don't interpolate across it */
} interp_cfg_t;

typedef struct {
    float    pos[3];
    float    vel[3];
    uint16_t heading;
    int32_t  relobj;
    double   delay_ms;        /* render delay used */
    int      mode;            /* 0 interp, 1 extrapolated, 2 held, 3 single snapshot */
} interp_out_t;

void interp_reset(interp_t *b);
/* Add a snapshot received at t_ms. Starts a new path on relobj change or teleport. */
void interp_push(interp_t *b, const interp_cfg_t *c, const snap_t *s);
/* State to display at local time now_ms. Returns 0 if the buffer is empty. */
int  interp_sample(const interp_t *b, const interp_cfg_t *c, double now_ms, interp_out_t *o);
double interp_delay(const interp_t *b, const interp_cfg_t *c);
