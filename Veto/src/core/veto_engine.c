#include "veto_engine.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>

static veto_engine *g_engine = NULL;

static void signal_handler(int sig) {
    if (g_engine) {
        fprintf(stdout, "\n[Signal] Caught signal %d, stopping...\n", sig);
        veto_engine_stop(g_engine);
    }
}

veto_engine *veto_engine_create(const veto_config *cfg) {
    veto_engine *eng = calloc(1, sizeof(veto_engine));
    if (!eng) return NULL;

    eng->config = (veto_config *)cfg;

    eng->capture = veto_capture_create(CAPTURE_FILTER_ALL);
    if (!eng->capture) {
        free(eng);
        return NULL;
    }

    size_t max_streams = cfg->max_streams > 0 ? cfg->max_streams : 4096;
    uint32_t timeout = cfg->reassembly_timeout_ms > 0 ? cfg->reassembly_timeout_ms : 30000;
    eng->reassembler = veto_reassembler_create(max_streams, timeout);
    if (!eng->reassembler) {
        veto_capture_destroy(eng->capture);
        free(eng);
        return NULL;
    }

    eng->lua_ctx = veto_lua_create();
    if (!eng->lua_ctx) {
        fprintf(stderr, "[WARNING] Failed to create Lua context\n");
    }

    return eng;
}

void veto_engine_destroy(veto_engine *eng) {
    if (!eng) return;
    veto_capture_destroy(eng->capture);
    veto_reassembler_destroy(eng->reassembler);
    veto_lua_destroy(eng->lua_ctx);
    free(eng);
}

bool veto_engine_load_lua(veto_engine *eng, const char *script_path) {
    if (!eng || !eng->lua_ctx || !script_path) return false;

    if (!veto_lua_init(eng->lua_ctx)) {
        fprintf(stderr, "[ERROR] Failed to initialize Lua\n");
        return false;
    }

    if (!veto_lua_load_script(eng->lua_ctx, script_path)) {
        fprintf(stderr, "[ERROR] Failed to load Lua script: %s\n", script_path);
        return false;
    }

    strncpy(eng->lua_script, script_path, sizeof(eng->lua_script) - 1);
    printf("[%s] Loaded Lua script: %s\n", VETO_NAME, script_path);
    return true;
}

static void packet_callback(veto_packet *pkt, const WINDIVERT_ADDRESS_PACKED *addr, void *user_ctx) {
    veto_engine *eng = (veto_engine *)user_ctx;
    if (!eng || !pkt) return;

    eng->packets_processed++;

    static DWORD last_stats = 0;
    DWORD now = GetTickCount();
    if (now - last_stats >= 3000) {
        printf("\r  Processed: %llu  Modified: %llu  Faked: %llu  Dropped: %llu   ",
               (unsigned long long)eng->packets_processed,
               (unsigned long long)eng->packets_modified,
               (unsigned long long)eng->packets_faked,
               (unsigned long long)eng->packets_dropped);
        fflush(stdout);
        last_stats = now;
    }

    if (pkt->meta.protocol != IPPROTO_TCP && pkt->meta.protocol != IPPROTO_UDP) {
        veto_capture_inject(eng->capture, pkt->raw_ip, pkt->raw_ip_len, addr);
        veto_packet_free(pkt);
        return;
    }

    if (pkt->meta.protocol == IPPROTO_UDP) {
        if (eng->active_attack && pkt->data && pkt->len > 0) {
            veto_proto_info info = veto_proto_detect(pkt->data, pkt->len);

            if (info.proto == PROTO_DNS) {
                for (int i = 0; i < eng->active_attack->rule_count; i++) {
                    if (eng->active_attack->rules[i].type == ATTACK_DNS_HIJACK &&
                        eng->active_attack->rules[i].enabled) {
                        eng->packets_modified++;
                        break;
                    }
                }
            }
        }

        veto_capture_inject(eng->capture, pkt->raw_ip, pkt->raw_ip_len, addr);
        veto_packet_free(pkt);
        return;
    }

    if (pkt->meta.protocol == IPPROTO_TCP) {
        veto_capture_inject(eng->capture, pkt->raw_ip, pkt->raw_ip_len, addr);

        if (pkt->meta.dir == DIR_OUTGOING && pkt->data && pkt->len > 0) {
            uint8_t *reassembled = NULL;
            size_t reassembled_len = 0;

            veto_reassembly_result ra_result = veto_reassembler_process(
                eng->reassembler, pkt, &reassembled, &reassembled_len);

            if (ra_result == REASSEMBLY_COMPLETE && reassembled && reassembled_len > 0) {
                veto_proto_info info = veto_proto_detect(reassembled, reassembled_len);

                bool attacked = false;

                if (eng->lua_ctx && eng->lua_ctx->initialized &&
                    (info.proto == PROTO_TLS || info.proto == PROTO_HTTP)) {

                    veto_packet reassembled_pkt = *pkt;
                    reassembled_pkt.data = reassembled;
                    reassembled_pkt.len = reassembled_len;

                    veto_lua_result lua_result = veto_lua_process_packet(
                        eng->lua_ctx, &reassembled_pkt, &info, pkt->meta.dir);

                    eng->lua_calls++;

                    switch (lua_result.verdict) {
                        case LUA_VERDICT_MODIFY:
                            eng->packets_modified++;
                            if (lua_result.data && lua_result.len > 0) {
                                veto_capture_inject(eng->capture, lua_result.data, lua_result.len, addr);
                            }
                            free(lua_result.data);
                            attacked = true;
                            break;

                        case LUA_VERDICT_FAKE:
                            eng->packets_faked++;
                            if (lua_result.has_fake && lua_result.fake_data) {
                                veto_capture_inject(eng->capture, lua_result.fake_data, lua_result.fake_len, addr);
                            }
                            free(lua_result.fake_data);
                            attacked = true;
                            break;

                        case LUA_VERDICT_DROP:
                            eng->packets_dropped++;
                            attacked = true;
                            break;

                        case LUA_VERDICT_PASS:
                        default:
                            break;
                    }
                    free(lua_result.data);
                }

                if (!attacked && eng->active_attack &&
                    (info.proto == PROTO_TLS || info.proto == PROTO_HTTP)) {

                    veto_packet reassembled_pkt = *pkt;
                    reassembled_pkt.data = reassembled;
                    reassembled_pkt.len = reassembled_len;

                    veto_attack_result attack = veto_attack_execute(
                        eng->active_attack, &reassembled_pkt, &info);

                    switch (attack.verdict) {
                        case VERDICT_MODIFY:
                            eng->packets_modified++;
                            if (attack.packet_data && attack.packet_len > 0) {
                                veto_capture_inject(eng->capture, attack.packet_data, attack.packet_len, addr);
                            }
                            free(attack.packet_data);
                            break;

                        case VERDICT_FAKE:
                            eng->packets_faked++;
                            if (attack.send_fake && attack.fake_data) {
                                veto_capture_inject(eng->capture, attack.fake_data, attack.fake_len, addr);
                            }
                            free(attack.fake_data);
                            break;

                        case VERDICT_DROP:
                            eng->packets_dropped++;
                            break;

                        case VERDICT_PASS:
                        default:
                            break;
                    }
                    free(attack.packet_data);
                }
                free(reassembled);
            }
        }
    }

    veto_packet_free(pkt);
}

veto_status veto_engine_start(veto_engine *eng) {
    if (!eng) return VETO_ERR_INVALID;

    g_engine = eng;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("[%s] Starting engine...\n", VETO_NAME);

    veto_status st = veto_capture_start(eng->capture, packet_callback, eng);
    if (st != VETO_OK) {
        fprintf(stderr, "[ERROR] Failed to start capture\n");
        return st;
    }

    eng->running = true;

    printf("[%s] Engine started successfully\n", VETO_NAME);
    printf("[%s] Listening on ports 80, 443 (TCP+UDP)\n", VETO_NAME);
    if (eng->active_attack) {
        for (int i = 0; i < eng->active_attack->rule_count; i++) {
            if (eng->active_attack->rules[i].type == ATTACK_DNS_HIJACK) {
                printf("[%s] DNS hijacking enabled\n", VETO_NAME);
            }
            if (eng->active_attack->rules[i].type == ATTACK_UDP_FRAG) {
                printf("[%s] UDP fragmentation enabled\n", VETO_NAME);
            }
        }
    }
    if (eng->lua_ctx && eng->lua_ctx->initialized) {
        printf("[%s] Lua script: %s\n", VETO_NAME, eng->lua_script);
    }
    printf("[%s] Press Ctrl+C to stop\n\n", VETO_NAME);

    veto_capture_run(eng->capture);

    return VETO_OK;
}

void veto_engine_stop(veto_engine *eng) {
    if (!eng) return;
    eng->running = false;
    veto_capture_stop(eng->capture);
    printf("[%s] Engine stopped\n", VETO_NAME);
}

void veto_engine_stats(const veto_engine *eng, uint64_t *processed,
                       uint64_t *modified, uint64_t *faked, uint64_t *dropped)
{
    if (!eng) return;
    if (processed) *processed = eng->packets_processed;
    if (modified) *modified = eng->packets_modified;
    if (faked) *faked = eng->packets_faked;
    if (dropped) *dropped = eng->packets_dropped;
}
