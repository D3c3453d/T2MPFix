/* Host-side tests for interp.c: gcc -O1 -I../src test_interp.c ../src/interp.c -lm && ./a.out */
#include "interp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const interp_cfg_t CFG = { 60, 300, 50, 100, 20.0f };

static snap_t mk(double t, float x, float y, float z, float vx, float vy, float vz, uint16_t h)
{
    snap_t s = { t, { x, y, z }, { vx, vy, vz }, h, 0 };
    return s;
}

/* A player walking at 15 u/s along x, sending every ~50 ms with +-10 ms jitter: the
 * displayed position equals the true position delay ms ago. */
static void test_steady_walk(void)
{
    interp_t b; interp_reset(&b);
    srand(1);
    double t = 0, maxerr = 0;
    for (int i = 0; i < 200; i++) {
        t += 50 + (rand() % 21 - 10);
        snap_t s = mk(t, (float)(15 * t / 1000), 0, 0, 15, 0, 0, 0);
        interp_push(&b, &CFG, &s);
        if (i < 5) continue;
        for (double now = t; now < t + 40; now += 7) {
            interp_out_t o;
            CHECK(interp_sample(&b, &CFG, now, &o), "sample failed");
            double truth = 15 * (now - o.delay_ms) / 1000;
            double err = fabs(o.pos[0] - truth);
            if (o.mode == 0 && err > maxerr) maxerr = err;
        }
    }
    CHECK(maxerr < 0.01, "steady walk error %.4f", maxerr);
}

/* Idle 5 s, then walk: the avatar must not slide slowly across the 5 s gap. */
static void test_idle_then_move(void)
{
    interp_t b; interp_reset(&b);
    snap_t s0 = mk(1000, 0, 0, 0, 0, 0, 0, 0);
    interp_push(&b, &CFG, &s0);
    snap_t s1 = mk(6000, 0.75f, 0, 0, 15, 0, 0, 0);
    interp_push(&b, &CFG, &s1);
    interp_out_t o;
    /* 4 s after s0 the player was still standing at 0 */
    interp_sample(&b, &CFG, 5000 + interp_delay(&b, &CFG), &o);
    CHECK(fabsf(o.pos[0]) < 1e-4f, "slid during idle gap: x=%.3f", o.pos[0]);
}

/* A teleport (respawn, FM teleporter) is shown as a jump, never as a 30 m slide. */
static void test_teleport_snaps(void)
{
    interp_t b; interp_reset(&b);
    for (int i = 0; i < 5; i++) { snap_t s = mk(i * 50, 0, 0, 0, 0, 0, 0, 0); interp_push(&b, &CFG, &s); }
    snap_t far = mk(250, 100, 0, 0, 0, 0, 0, 0);
    interp_push(&b, &CFG, &far);
    interp_out_t o;
    for (double now = 250; now < 600; now += 10) {
        interp_sample(&b, &CFG, now, &o);
        CHECK(o.pos[0] == 0 || o.pos[0] == 100, "intermediate position %.2f during teleport", o.pos[0]);
    }
    CHECK(o.pos[0] == 100, "did not arrive after teleport");
}

/* Heading crosses 0: interpolate the short way round. */
static void test_heading_wrap(void)
{
    interp_t b; interp_reset(&b);
    for (int i = 0; i < 4; i++) { snap_t s = mk(i * 50, 0, 0, 0, 0, 0, 0, 65000); interp_push(&b, &CFG, &s); }
    snap_t s = mk(200, 0, 0, 0, 0, 0, 0, 500); interp_push(&b, &CFG, &s);
    interp_out_t o;
    double d = interp_delay(&b, &CFG);
    interp_sample(&b, &CFG, 175 + d, &o);
    int dist = (int16_t)(uint16_t)(o.heading - 65000);
    CHECK(dist > 0 && dist < 1100, "heading went the long way: %u", o.heading);
}

/* Starved buffer: extrapolate a little, then hold still (no flying off). */
static void test_extrapolation_cap(void)
{
    interp_t b; interp_reset(&b);
    for (int i = 0; i < 5; i++) { snap_t s = mk(i * 50, (float)(i * 0.75), 0, 0, 15, 0, 0, 0); interp_push(&b, &CFG, &s); }
    interp_out_t o;
    interp_sample(&b, &CFG, 200 + interp_delay(&b, &CFG) + 5000, &o);
    CHECK(o.mode == 2, "mode %d", o.mode);
    CHECK(fabsf(o.pos[0] - (3.0f + 1.5f)) < 1e-3f, "held at %.3f, want 4.5", o.pos[0]);
    CHECK(o.vel[0] == 0, "held avatar still has velocity");
}

/* Delay adapts to jitter but stays inside the configured bounds. */
static void test_delay_bounds(void)
{
    interp_t b; interp_reset(&b);
    double t = 0;
    for (int i = 0; i < 100; i++) { t += (i % 2) ? 20 : 140; snap_t s = mk(t, (float)i, 0, 0, 1, 0, 0, 0); interp_push(&b, &CFG, &s); }
    double d = interp_delay(&b, &CFG);
    CHECK(d >= CFG.delay_min_ms && d <= CFG.delay_max_ms, "delay %.1f out of bounds", d);
    CHECK(d > 150, "delay %.1f does not cover 140 ms gaps", d);
}

int main(void)
{
    test_steady_walk();
    test_idle_then_move();
    test_teleport_snaps();
    test_heading_wrap();
    test_extrapolation_cap();
    test_delay_bounds();
    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails != 0;
}
