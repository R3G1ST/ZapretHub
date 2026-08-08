#ifndef VETO_LUA_H
#define VETO_LUA_H

#include "veto.h"
#include "packet.h"
#include "proto_detect.h"
#include "attacks.h"

#define LUA_USE_POSIX
#define LUA_IMPLEMENTATION

#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"

typedef enum {
    LUA_VERDICT_PASS = 0,
    LUA_VERDICT_MODIFY = 1,
    LUA_VERDICT_DROP = 2,
    LUA_VERDICT_FAKE = 3
} veto_lua_verdict;

typedef struct {
    lua_State *L;
    char script_path[256];
    bool initialized;
    int strategy_ref;
} veto_lua_ctx;

typedef struct {
    veto_lua_verdict verdict;
    uint8_t *data;
    size_t len;
    uint8_t *fake_data;
    size_t fake_len;
    bool has_fake;
} veto_lua_result;

veto_lua_ctx *veto_lua_create(void);
void veto_lua_destroy(veto_lua_ctx *ctx);

bool veto_lua_load_script(veto_lua_ctx *ctx, const char *path);
bool veto_lua_init(veto_lua_ctx *ctx);

veto_lua_result veto_lua_process_packet(
    veto_lua_ctx *ctx,
    const veto_packet *pkt,
    const veto_proto_info *info,
    veto_direction dir
);

void veto_lua_push_packet(lua_State *L, const veto_packet *pkt, const veto_proto_info *info);
void veto_lua_push_proto_info(lua_State *L, const veto_proto_info *info);

#endif
