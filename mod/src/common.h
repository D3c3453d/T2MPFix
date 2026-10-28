/* Helpers shared between main.c, the fix modules (fix_*.c) and qa.c */
#pragma once
#include <stdint.h>
#include <stdio.h>

double now_ms(void);
uint32_t sim_ms(void);
void logf_(const char *fmt, ...);
void command_execute(const char *cmd);   /* engine command line; game thread only */
void run_verb(const char *cmd);          /* "bind ...", "mark ...", or engine command */
void qa_init(const char *game_dir, int qa_enabled, int need_tick);   /* installs the message-loop tick */
void game_tick(void);
int qa_local_player(void);
extern FILE *g_tel;
extern int g_hook_mask;                  /* [qa] hooks mask, HK_* in fixes.h */

/* Engine helpers */
float *model_pos(int obj);               /* physics model position, NULL if none */
int16_t *obj_facing(int obj);
void obj_pos_update(int obj, const float *pos, const int16_t *facing);   /* unconditional placement */
float len3(const float *v);

/* Byte signatures and hooks */
int sig_ok(uintptr_t va, const char *sig, size_t n);
int sig_rebased(uintptr_t va, const char *sig, size_t n);   /* bytes 1..4 are an absolute address */
#define SIG(va, s) sig_ok(va, s, sizeof(s) - 1)
const char *sig_mismatch(uintptr_t va);  /* "signature mismatch at 0x..., left stock" */
int hook(uintptr_t va, void *det, void **orig, const char *name);
int patch_bytes(uintptr_t va, const void *bytes, size_t n);   /* write into the exe's code, logged */
