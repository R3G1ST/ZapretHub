#include "veto_autotune.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#define TEST_TIMEOUT_MS 3000
#define TEST_PORT 443

static const char *DEFAULT_TEST_DOMAINS[] = {
    "youtube.com",
    "discord.com",
    "twitter.com",
    "x.com",
    "t.co",
    "google.com",
    NULL
};

veto_autotune_ctx *veto_autotune_create(void) {
    veto_autotune_ctx *ctx = calloc(1, sizeof(veto_autotune_ctx));
    if (!ctx) return NULL;
    ctx->result.best_strategy = -1;
    return ctx;
}

void veto_autotune_destroy(veto_autotune_ctx *ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->result.strategy_count; i++) {
        if (ctx->result.strategies[i].config) {
            veto_attack_config_destroy(ctx->result.strategies[i].config);
        }
    }
    free(ctx);
}

bool veto_autotune_add_test_domain(veto_autotune_ctx *ctx, const char *domain) {
    if (!ctx || !domain) return false;
    if (ctx->test_domain_count >= 16) return false;
    strncpy(ctx->test_domains[ctx->test_domain_count], domain, 127);
    ctx->test_domains[ctx->test_domain_count][127] = '\0';
    ctx->test_domain_count++;
    return true;
}

bool veto_autotune_add_strategy(veto_autotune_ctx *ctx, const char *name, veto_attack_config *cfg) {
    if (!ctx || !name || !cfg) return false;
    if (ctx->result.strategy_count >= 32) return false;

    int idx = ctx->result.strategy_count;
    strncpy(ctx->result.strategies[idx].name, name, 63);
    ctx->result.strategies[idx].name[63] = '\0';
    ctx->result.strategies[idx].config = cfg;
    ctx->result.strategy_count++;
    return true;
}

static double test_connection(const char *host, int port, int timeout_ms) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;

    struct hostent *he = gethostbyname(host);
    if (!he) {
        WSACleanup();
        return -1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);

    DWORD start = GetTickCount();
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(sock);
        WSACleanup();
        return -1;
    }
    DWORD elapsed = GetTickCount() - start;

    closesocket(sock);
    WSACleanup();
    return (double)elapsed;
}

static int extract_ip_from_hostent(struct hostent *he) {
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) return 0;
    return *(int*)he->h_addr_list[0];
}

static double test_connection_with_fake(const char *host, int port, int timeout_ms, veto_attack_config *cfg) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;

    struct hostent *he = gethostbyname(host);
    if (!he) {
        WSACleanup();
        return -1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);

    DWORD start = GetTickCount();
    int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    DWORD elapsed = GetTickCount() - start;

    closesocket(sock);
    WSACleanup();

    if (result != 0) return -1;
    return (double)elapsed;
}

static void load_default_strategies(veto_autotune_ctx *ctx) {
    veto_attack_config *fake_cfg = veto_attack_config_create();
    fake_cfg->rule_count = 1;
    fake_cfg->rules[0].type = ATTACK_FAKE;
    fake_cfg->rules[0].fool = FOOL_TTL;
    fake_cfg->rules[0].fake_ttl = 4;
    fake_cfg->rules[0].enabled = true;
    veto_autotune_add_strategy(ctx, "fake_ttl4", fake_cfg);

    veto_attack_config *split_cfg = veto_attack_config_create();
    split_cfg->rule_count = 1;
    split_cfg->rules[0].type = ATTACK_SPLIT;
    split_cfg->rules[0].split_pos[0] = 6;
    split_cfg->rules[0].enabled = true;
    veto_autotune_add_strategy(ctx, "split_6", split_cfg);

    veto_attack_config *disorder_cfg = veto_attack_config_create();
    disorder_cfg->rule_count = 1;
    disorder_cfg->rules[0].type = ATTACK_DISORDER;
    disorder_cfg->rules[0].split_pos[0] = 1;
    disorder_cfg->rules[0].split_pos[1] = 6;
    disorder_cfg->rules[0].enabled = true;
    veto_autotune_add_strategy(ctx, "disorder_1_6", disorder_cfg);

    veto_attack_config *fake2_cfg = veto_attack_config_create();
    fake2_cfg->rule_count = 1;
    fake2_cfg->rules[0].type = ATTACK_FAKE;
    fake2_cfg->rules[0].fool = FOOL_TTL;
    fake2_cfg->rules[0].fake_ttl = 2;
    fake2_cfg->rules[0].enabled = true;
    veto_autotune_add_strategy(ctx, "fake_ttl2", fake2_cfg);

    veto_attack_config *split2_cfg = veto_attack_config_create();
    split2_cfg->rule_count = 1;
    split2_cfg->rules[0].type = ATTACK_SPLIT;
    split2_cfg->rules[0].split_pos[0] = 12;
    split2_cfg->rules[0].enabled = true;
    veto_autotune_add_strategy(ctx, "split_12", split2_cfg);

    veto_attack_config *ipfrag_cfg = veto_attack_config_create();
    ipfrag_cfg->rule_count = 1;
    ipfrag_cfg->rules[0].type = ATTACK_IP_FRAG;
    ipfrag_cfg->rules[0].split_pos[0] = 64;
    ipfrag_cfg->rules[0].enabled = true;
    veto_autotune_add_strategy(ctx, "ipfrag_64", ipfrag_cfg);

    veto_attack_config *rst_cfg = veto_attack_config_create();
    rst_cfg->rule_count = 1;
    rst_cfg->rules[0].type = ATTACK_RST;
    rst_cfg->rules[0].enabled = true;
    veto_autotune_add_strategy(ctx, "rst", rst_cfg);
}

veto_autotune_result veto_autotune_run(veto_autotune_ctx *ctx) {
    if (!ctx) return ctx->result;

    ctx->running = true;
    ctx->result.completed = false;

    if (ctx->test_domain_count == 0) {
        for (int i = 0; DEFAULT_TEST_DOMAINS[i]; i++) {
            veto_autotune_add_test_domain(ctx, DEFAULT_TEST_DOMAINS[i]);
        }
    }

    if (ctx->result.strategy_count == 0) {
        load_default_strategies(ctx);
    }

    printf("[Autotune] Testing %d strategies on %d domains...\n",
           ctx->result.strategy_count, ctx->test_domain_count);

    printf("\n[Step 1] Baseline test (no bypass)...\n");
    double baseline_total = 0;
    int baseline_ok = 0;

    for (int d = 0; d < ctx->test_domain_count; d++) {
        if (!ctx->running) break;
        double latency = test_connection(ctx->test_domains[d], TEST_PORT, TEST_TIMEOUT_MS);
        if (latency >= 0) {
            baseline_total += latency;
            baseline_ok++;
            printf("  %s: %.0fms\n", ctx->test_domains[d], latency);
        } else {
            printf("  %s: BLOCKED\n", ctx->test_domains[d]);
        }
    }

    double baseline_avg = baseline_ok > 0 ? baseline_total / baseline_ok : -1;
    printf("  Baseline average: %.0fms (%d/%d succeeded)\n", baseline_avg, baseline_ok, ctx->test_domain_count);

    printf("\n[Step 2] Testing strategies...\n");

    for (int s = 0; s < ctx->result.strategy_count; s++) {
        if (!ctx->running) break;

        veto_strategy_test *st = &ctx->result.strategies[s];
        printf("\n  Testing: %s\n", st->name);

        double total_latency = 0;
        int success_count = 0;
        int test_count = 0;

        for (int d = 0; d < ctx->test_domain_count; d++) {
            if (!ctx->running) break;

            double latency = test_connection_with_fake(
                ctx->test_domains[d], TEST_PORT, TEST_TIMEOUT_MS, st->config);

            test_count++;
            if (latency >= 0) {
                total_latency += latency;
                success_count++;
                printf("    %s: %.0fms\n", ctx->test_domains[d], latency);
            } else {
                printf("    %s: FAIL\n", ctx->test_domains[d]);
            }

            Sleep(100);
        }

        st->tested = true;
        st->latency_ms = success_count > 0 ? total_latency / success_count : -1;

        if (baseline_ok > 0) {
            if (success_count == ctx->test_domain_count) {
                st->score = 100;
                if (st->latency_ms < baseline_avg * 1.2) st->score += 20;
            } else if (success_count > 0) {
                st->score = (success_count * 100) / ctx->test_domain_count;
            } else {
                st->score = 0;
            }
        } else {
            st->score = success_count * 20;
        }

        st->works = success_count > 0;
        printf("    Result: %s (score: %d, avg: %.0fms)\n",
               st->works ? "OK" : "FAIL", st->score, st->latency_ms);
    }

    printf("\n[Step 3] Selecting best strategy...\n");

    int best = -1;
    int best_score = -1;
    for (int s = 0; s < ctx->result.strategy_count; s++) {
        if (ctx->result.strategies[s].tested && ctx->result.strategies[s].score > best_score) {
            best_score = ctx->result.strategies[s].score;
            best = s;
        }
    }

    ctx->result.best_strategy = best;
    ctx->result.completed = true;
    ctx->running = false;

    if (best >= 0) {
        printf("\n  BEST: %s (score: %d)\n",
               ctx->result.strategies[best].name,
               ctx->result.strategies[best].score);
    } else {
        printf("\n  No working strategy found\n");
    }

    return ctx->result;
}

void veto_autotune_stop(veto_autotune_ctx *ctx) {
    if (ctx) ctx->running = false;
}

bool veto_autotune_save_best(const veto_autotune_result *result, const char *path) {
    if (!result || !path || result->best_strategy < 0) return false;

    const veto_strategy_test *best = &result->strategies[result->best_strategy];
    const veto_attack_config *cfg = best->config;
    if (!cfg) return false;

    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "# Auto-generated by Veto Autotune\n");
    fprintf(f, "# Best strategy: %s (score: %d)\n\n", best->name, best->score);

    fprintf(f, "[capture]\n");
    fprintf(f, "mode = windivert\n\n");

    fprintf(f, "[attack]\n");

    for (int i = 0; i < cfg->rule_count; i++) {
        const veto_attack_rule *rule = &cfg->rules[i];
        if (!rule->enabled) continue;

        switch (rule->type) {
            case ATTACK_FAKE:
                fprintf(f, "fake = ");
                if (rule->fool == FOOL_TTL) {
                    fprintf(f, "ttl:%d", rule->fake_ttl);
                } else if (rule->fool == FOOL_BAD_SUM) {
                    fprintf(f, "checksum");
                } else if (rule->fool == FOOL_TS) {
                    fprintf(f, "ts");
                }
                fprintf(f, "\n");
                break;

            case ATTACK_SPLIT:
                fprintf(f, "split = ");
                for (int j = 0; j < rule->split_count; j++) {
                    if (j > 0) fprintf(f, ",");
                    fprintf(f, "%d", rule->split_pos[j]);
                }
                fprintf(f, "\n");
                break;

            case ATTACK_DISORDER:
                fprintf(f, "disorder = ");
                for (int j = 0; j < rule->split_count; j++) {
                    if (j > 0) fprintf(f, ",");
                    fprintf(f, "%d", rule->split_pos[j]);
                }
                fprintf(f, "\n");
                break;

            case ATTACK_FAKED_SPLIT:
                fprintf(f, "fakedsplit = ");
                for (int j = 0; j < rule->split_count; j++) {
                    if (j > 0) fprintf(f, ",");
                    fprintf(f, "%d", rule->split_pos[j]);
                }
                fprintf(f, "\n");
                break;

            default:
                break;
        }
    }

    fprintf(f, "\n[streams]\n");
    fprintf(f, "max = 4096\n");
    fprintf(f, "timeout = 30000\n");

    fclose(f);
    return true;
}

void veto_autotune_print_result(const veto_autotune_result *result) {
    if (!result) return;

    printf("\n=== Autotune Results ===\n");
    printf("Completed: %s\n", result->completed ? "Yes" : "No");
    printf("Strategies tested: %d\n", result->strategy_count);

    printf("\nStrategy scores:\n");
    for (int i = 0; i < result->strategy_count; i++) {
        const veto_strategy_test *st = &result->strategies[i];
        printf("  %-20s %s  score=%d  avg=%.0fms\n",
               st->name,
               st->works ? "OK  " : "FAIL",
               st->score,
               st->latency_ms);
    }

    if (result->best_strategy >= 0) {
        printf("\nRecommended: %s\n", result->strategies[result->best_strategy].name);
    }
}
