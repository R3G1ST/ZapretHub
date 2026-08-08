#ifndef VETO_AUTOTUNE_H
#define VETO_AUTOTUNE_H

#include "veto.h"
#include "attacks.h"

typedef struct {
    char name[64];
    veto_attack_config *config;
    bool tested;
    bool works;
    double latency_ms;
    int score;
} veto_strategy_test;

typedef struct {
    char provider[128];
    char country[32];
    veto_strategy_test strategies[32];
    int strategy_count;
    int best_strategy;
    bool completed;
} veto_autotune_result;

typedef struct {
    veto_autotune_result result;
    bool running;
    int current_test;
    char test_domains[16][128];
    int test_domain_count;
} veto_autotune_ctx;

veto_autotune_ctx *veto_autotune_create(void);
void veto_autotune_destroy(veto_autotune_ctx *ctx);

bool veto_autotune_add_test_domain(veto_autotune_ctx *ctx, const char *domain);
bool veto_autotune_add_strategy(veto_autotune_ctx *ctx, const char *name, veto_attack_config *cfg);

veto_autotune_result veto_autotune_run(veto_autotune_ctx *ctx);
void veto_autotune_stop(veto_autotune_ctx *ctx);

bool veto_autotune_save_best(const veto_autotune_result *result, const char *path);
void veto_autotune_print_result(const veto_autotune_result *result);

#endif
