#include "veto_engine.h"
#include "veto_autotune.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_banner(void) {
    printf("==============================================\n");
    printf("  %s v%s\n", VETO_NAME, VETO_VERSION);
    printf("  DPI Bypass Engine\n");
    printf("==============================================\n");
    fflush(stdout);
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -c, --config <path>    Load config file\n");
    printf("  -l, --lua <script>     Load Lua strategy script\n");
    printf("  -p, --port <port>      Listen port (default: all)\n");
    printf("  --autotune             Auto-detect best strategy\n");
    printf("  --save-config <path>   Save autotune result to config\n");
    printf("  --udp                  Enable UDP capture (QUIC, DNS)\n");
    printf("  --dns-hijack           Enable DNS hijacking\n");
    printf("  -v, --verbose          Verbose output\n");
    printf("  -h, --help             Show this help\n");
    printf("\n");
    printf("Attack modes (with --attack):\n");
    printf("  fake                   Send fake packets\n");
    printf("  split                  Split TCP segments\n");
    printf("  disorder               Out-of-order segments\n");
    printf("  fakedsplit             Fake + split\n");
    printf("  ipfrag                 IP fragmentation\n");
    printf("  udpfrag                UDP fragmentation\n");
    printf("  dns                    DNS hijack\n");
    printf("  tamper                 Tamper payload\n");
    printf("  rst                    RST injection\n");
    printf("  wsize                  Window size manipulation\n");
    printf("\n");
    printf("Lua scripting:\n");
    printf("  --lua <script>         Load custom Lua strategy\n");
    printf("  Lua API: veto.VERDICT_PASS/MODIFY/DROP/FAKE\n");
    printf("           veto.split_packet(data, pos)\n");
    printf("           veto.make_fake(data, ttl)\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s --attack fake --hostlist youtube.txt\n", prog);
    printf("  %s --lua strategies/youtube-split.lua\n", prog);
    printf("  %s --autotune\n", prog);
    printf("  %s --config veto.conf\n", prog);
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    print_banner();

    veto_config *cfg = veto_config_create();
    if (!cfg) {
        fprintf(stderr, "Failed to create config\n");
        return 1;
    }

    veto_attack_config *attacks = veto_attack_config_create();
    if (!attacks) {
        fprintf(stderr, "Failed to create attack config\n");
        veto_config_destroy(cfg);
        return 1;
    }

    char lua_script[256] = {0};
    char save_config_path[256] = {0};
    bool enable_udp = false;
    bool enable_dns_hijack = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            veto_config_destroy(cfg);
            veto_attack_config_destroy(attacks);
            return 0;
        }
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            cfg->verbose = true;
        }
        else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                strncpy(cfg->config_path, argv[++i], sizeof(cfg->config_path) - 1);
            }
        }
        else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--lua") == 0) {
            if (i + 1 < argc) {
                strncpy(lua_script, argv[++i], sizeof(lua_script) - 1);
            }
        }
        else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc) {
                cfg->listen_port = (uint16_t)atoi(argv[++i]);
            }
        }
        else if (strcmp(argv[i], "--attack") == 0) {
            if (i + 1 < argc) {
                const char *type = argv[++i];
                int idx = attacks->rule_count;
                if (idx < 16) {
                    attacks->rules[idx].enabled = true;
                    attacks->rule_count++;
                    if (strcmp(type, "fake") == 0) {
                        attacks->rules[idx].type = ATTACK_FAKE;
                        attacks->rules[idx].fool = FOOL_TTL;
                        attacks->rules[idx].fake_ttl = 4;
                    } else if (strcmp(type, "split") == 0) {
                        attacks->rules[idx].type = ATTACK_SPLIT;
                        attacks->rules[idx].split_pos[0] = 6;
                    } else if (strcmp(type, "disorder") == 0) {
                        attacks->rules[idx].type = ATTACK_DISORDER;
                        attacks->rules[idx].split_pos[0] = 1;
                        attacks->rules[idx].split_pos[1] = 6;
                    } else if (strcmp(type, "fakedsplit") == 0) {
                        attacks->rules[idx].type = ATTACK_FAKED_SPLIT;
                    } else if (strcmp(type, "udpfrag") == 0) {
                        attacks->rules[idx].type = ATTACK_UDP_FRAG;
                        attacks->rules[idx].split_pos[0] = 128;
                    } else if (strcmp(type, "dns") == 0) {
                        attacks->rules[idx].type = ATTACK_DNS_HIJACK;
                    } else if (strcmp(type, "ipfrag") == 0) {
                        attacks->rules[idx].type = ATTACK_IP_FRAG;
                        attacks->rules[idx].split_pos[0] = 64;
                    } else if (strcmp(type, "tamper") == 0) {
                        attacks->rules[idx].type = ATTACK_TAMPER;
                        attacks->rules[idx].fool = FOOL_BAD_SUM;
                    } else if (strcmp(type, "rst") == 0) {
                        attacks->rules[idx].type = ATTACK_RST;
                    } else if (strcmp(type, "wsize") == 0) {
                        attacks->rules[idx].type = ATTACK_WSIZE;
                        attacks->rules[idx].fake_port = 1024;
                    }
                }
            }
        }
        else if (strcmp(argv[i], "--hostlist") == 0) {
            if (i + 1 < argc) {
                veto_attack_load_hostlist(attacks, argv[++i]);
            }
        }
        else if (strcmp(argv[i], "--save-config") == 0) {
            if (i + 1 < argc) {
                strncpy(save_config_path, argv[++i], sizeof(save_config_path) - 1);
            }
        }
        else if (strcmp(argv[i], "--udp") == 0) {
            enable_udp = true;
        }
        else if (strcmp(argv[i], "--dns-hijack") == 0) {
            enable_dns_hijack = true;
            int idx = attacks->rule_count;
            if (idx < 16) {
                attacks->rules[idx].type = ATTACK_DNS_HIJACK;
                attacks->rules[idx].enabled = true;
                attacks->rule_count++;
            }
        }
        else if (strcmp(argv[i], "--autotune") == 0) {
            printf("[Autotune] Starting auto-detection...\n\n");
            veto_autotune_ctx *at = veto_autotune_create();
            veto_autotune_result res = veto_autotune_run(at);
            veto_autotune_print_result(&res);
            if (res.best_strategy >= 0) {
                printf("\nApply with: --attack %s\n",
                       res.strategies[res.best_strategy].name);
                if (save_config_path[0]) {
                    if (veto_autotune_save_best(&res, save_config_path)) {
                        printf("Saved to: %s\n", save_config_path);
                    } else {
                        fprintf(stderr, "Failed to save config\n");
                    }
                }
            }
            veto_autotune_destroy(at);
            veto_config_destroy(cfg);
            veto_attack_config_destroy(attacks);
            return 0;
        }
    }

    cfg->max_streams = 4096;
    cfg->reassembly_timeout_ms = 30000;

    veto_engine *engine = veto_engine_create(cfg);
    if (!engine) {
        fprintf(stderr, "Failed to create engine\n");
        veto_config_destroy(cfg);
        veto_attack_config_destroy(attacks);
        return 1;
    }

    engine->active_attack = attacks;

    if (lua_script[0]) {
        if (!veto_engine_load_lua(engine, lua_script)) {
            fprintf(stderr, "Failed to load Lua script\n");
        }
    }

    printf("\nConfiguration:\n");
    printf("  Hostlist: %zu domains\n", attacks->hostlist_count);
    printf("  Attack rules: %d\n", attacks->rule_count);
    if (lua_script[0]) {
        printf("  Lua script: %s\n", lua_script);
    }
    printf("  Max streams: %zu\n", cfg->max_streams);
    printf("  Timeout: %u ms\n", cfg->reassembly_timeout_ms);
    printf("\n");

    veto_status st = veto_engine_start(engine);
    if (st != VETO_OK) {
        fprintf(stderr, "Failed to start engine: %d\n", st);
    }

    uint64_t processed, modified, faked, dropped;
    veto_engine_stats(engine, &processed, &modified, &faked, &dropped);
    printf("\nFinal stats:\n");
    printf("  Processed: %llu\n", processed);
    printf("  Modified:  %llu\n", modified);
    printf("  Faked:     %llu\n", faked);
    printf("  Dropped:   %llu\n", dropped);

    veto_engine_destroy(engine);
    veto_config_destroy(cfg);
    veto_attack_config_destroy(attacks);

    return 0;
}
