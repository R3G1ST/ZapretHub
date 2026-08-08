#ifndef VETO_ENGINE_H
#define VETO_ENGINE_H

#include "veto.h"
#include "config.h"
#include "capture.h"
#include "tcp_reassembly.h"
#include "proto_detect.h"
#include "attacks.h"
#include "veto_lua.h"

typedef struct {
    veto_config *config;
    veto_capture_ctx *capture;
    veto_reassembler *reassembler;
    veto_attack_config *active_attack;
    veto_lua_ctx *lua_ctx;
    char lua_script[256];
    volatile bool running;
    uint64_t packets_processed;
    uint64_t packets_modified;
    uint64_t packets_faked;
    uint64_t packets_dropped;
    uint64_t lua_calls;
} veto_engine;

veto_engine *veto_engine_create(const veto_config *cfg);
void veto_engine_destroy(veto_engine *eng);

veto_status veto_engine_start(veto_engine *eng);
void veto_engine_stop(veto_engine *eng);

bool veto_engine_load_lua(veto_engine *eng, const char *script_path);

void veto_engine_stats(const veto_engine *eng, uint64_t *processed,
                       uint64_t *modified, uint64_t *faked, uint64_t *dropped);

#endif
