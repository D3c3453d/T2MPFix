/* Helpers shared between main.c and qa.c */
#pragma once
#include <stdint.h>
#include <stdio.h>

double now_ms(void);
uint32_t sim_ms(void);
void logf_(const char *fmt, ...);
void command_execute(const char *cmd);   /* engine command line; game thread only */
void run_verb(const char *cmd);          /* "bind ...", "mark ...", or engine command */
void qa_init(const char *game_dir, int qa_enabled);   /* installs the message-loop tick */
void game_tick(void);
int qa_local_player(void);
extern FILE *g_tel;
