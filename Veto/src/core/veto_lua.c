#include "veto_lua.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static veto_lua_result make_lua_result(veto_lua_verdict v) {
    veto_lua_result r;
    memset(&r, 0, sizeof(r));
    r.verdict = v;
    return r;
}

veto_lua_ctx *veto_lua_create(void) {
    veto_lua_ctx *ctx = calloc(1, sizeof(veto_lua_ctx));
    if (!ctx) return NULL;
    ctx->L = luaL_newstate();
    if (!ctx->L) {
        free(ctx);
        return NULL;
    }
    luaL_openlibs(ctx->L);
    ctx->strategy_ref = LUA_NOREF;
    return ctx;
}

void veto_lua_destroy(veto_lua_ctx *ctx) {
    if (!ctx) return;
    if (ctx->L) {
        if (ctx->strategy_ref != LUA_NOREF) {
            luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->strategy_ref);
        }
        lua_close(ctx->L);
    }
    free(ctx);
}

static int lua_verdict_pass(lua_State *L) {
    lua_pushinteger(L, LUA_VERDICT_PASS);
    return 1;
}

static int lua_verdict_modify(lua_State *L) {
    lua_pushinteger(L, LUA_VERDICT_MODIFY);
    return 1;
}

static int lua_verdict_drop(lua_State *L) {
    lua_pushinteger(L, LUA_VERDICT_DROP);
    return 1;
}

static int lua_verdict_fake(lua_State *L) {
    lua_pushinteger(L, LUA_VERDICT_FAKE);
    return 1;
}

static int lua_get_sni(lua_State *L) {
    const char *sni = lua_tostring(L, 1);
    if (sni) {
        lua_pushstring(L, sni);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int lua_get_host(lua_State *L) {
    const char *host = lua_tostring(L, 1);
    if (host) {
        lua_pushstring(L, host);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int lua_split_packet(lua_State *L) {
    size_t len;
    const char *data = luaL_checklstring(L, 1, &len);
    int pos = luaL_checkinteger(L, 2);
    if (pos < 0 || (size_t)pos >= len) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, data, pos);
    lua_pushlstring(L, data + pos, len - pos);
    return 2;
}

static int lua_make_fake(lua_State *L) {
    size_t len;
    const char *data = luaL_checklstring(L, 1, &len);
    int ttl = luaL_optinteger(L, 2, 4);
    uint8_t *fake = malloc(len);
    if (!fake) {
        lua_pushnil(L);
        return 1;
    }
    memcpy(fake, data, len);
    if (len >= 20) {
        fake[8] = (uint8_t)ttl;
        fake[10] = 0;
        fake[11] = 0;
    }
    lua_pushlstring(L, (const char *)fake, len);
    free(fake);
    return 1;
}

static const luaL_Reg veto_funcs[] = {
    {"VERDICT_PASS", lua_verdict_pass},
    {"VERDICT_MODIFY", lua_verdict_modify},
    {"VERDICT_DROP", lua_verdict_drop},
    {"VERDICT_FAKE", lua_verdict_fake},
    {"get_sni", lua_get_sni},
    {"get_host", lua_get_host},
    {"split_packet", lua_split_packet},
    {"make_fake", lua_make_fake},
    {NULL, NULL}
};

bool veto_lua_load_script(veto_lua_ctx *ctx, const char *path) {
    if (!ctx || !path) return false;

    if (luaL_dofile(ctx->L, path) != LUA_OK) {
        fprintf(stderr, "[LUA ERROR] %s\n", lua_tostring(ctx->L, -1));
        lua_pop(ctx->L, 1);
        return false;
    }

    strncpy(ctx->script_path, path, sizeof(ctx->script_path) - 1);
    ctx->initialized = true;
    return true;
}

bool veto_lua_init(veto_lua_ctx *ctx) {
    if (!ctx || !ctx->L) return false;

    lua_newtable(ctx->L);
    int i = 0;
    while (veto_funcs[i].name) {
        lua_pushcfunction(ctx->L, veto_funcs[i].func);
        lua_setfield(ctx->L, -2, veto_funcs[i].name);
        i++;
    }
    lua_setglobal(ctx->L, "veto");

    lua_pushinteger(ctx->L, LUA_VERDICT_PASS);
    lua_setglobal(ctx->L, "VERDICT_PASS");
    lua_pushinteger(ctx->L, LUA_VERDICT_MODIFY);
    lua_setglobal(ctx->L, "VERDICT_MODIFY");
    lua_pushinteger(ctx->L, LUA_VERDICT_DROP);
    lua_setglobal(ctx->L, "VERDICT_DROP");
    lua_pushinteger(ctx->L, LUA_VERDICT_FAKE);
    lua_setglobal(ctx->L, "VERDICT_FAKE");

    lua_pushinteger(ctx->L, DIR_OUTGOING);
    lua_setglobal(ctx->L, "DIR_OUTGOING");
    lua_pushinteger(ctx->L, DIR_INCOMING);
    lua_setglobal(ctx->L, "DIR_INCOMING");

    lua_pushinteger(ctx->L, PROTO_TLS);
    lua_setglobal(ctx->L, "PROTO_TLS");
    lua_pushinteger(ctx->L, PROTO_HTTP);
    lua_setglobal(ctx->L, "PROTO_HTTP");
    lua_pushinteger(ctx->L, PROTO_QUIC);
    lua_setglobal(ctx->L, "PROTO_QUIC");

    return true;
}

void veto_lua_push_packet(lua_State *L, const veto_packet *pkt, const veto_proto_info *info) {
    lua_newtable(L);

    if (pkt->data && pkt->len > 0) {
        lua_pushlstring(L, (const char *)pkt->data, pkt->len);
        lua_setfield(L, -2, "data");
    }

    lua_pushinteger(L, pkt->len);
    lua_setfield(L, -2, "len");

    char src_ip[16], dst_ip[16];
    struct in_addr sa, da;
    sa.s_addr = pkt->meta.src_ip;
    da.s_addr = pkt->meta.dst_ip;
    strcpy(src_ip, inet_ntoa(sa));
    strcpy(dst_ip, inet_ntoa(da));

    lua_pushstring(L, src_ip);
    lua_setfield(L, -2, "src_ip");
    lua_pushstring(L, dst_ip);
    lua_setfield(L, -2, "dst_ip");

    lua_pushinteger(L, ntohs(pkt->meta.src_port));
    lua_setfield(L, -2, "src_port");
    lua_pushinteger(L, ntohs(pkt->meta.dst_port));
    lua_setfield(L, -2, "dst_port");

    lua_pushinteger(L, pkt->meta.protocol);
    lua_setfield(L, -2, "protocol");

    lua_pushinteger(L, pkt->meta.dir);
    lua_setfield(L, -2, "direction");
}

void veto_lua_push_proto_info(lua_State *L, const veto_proto_info *info) {
    lua_newtable(L);

    lua_pushinteger(L, info->proto);
    lua_setfield(L, -2, "proto");

    lua_pushboolean(L, info->is_client_hello);
    lua_setfield(L, -2, "is_client_hello");
    lua_pushboolean(L, info->is_server_hello);
    lua_setfield(L, -2, "is_server_hello");
    lua_pushboolean(L, info->is_http_request);
    lua_setfield(L, -2, "is_http_request");
    lua_pushboolean(L, info->is_http_response);
    lua_setfield(L, -2, "is_http_response");

    if (info->tls_sni[0]) {
        lua_pushstring(L, info->tls_sni);
        lua_setfield(L, -2, "sni");
    }
    if (info->http_host[0]) {
        lua_pushstring(L, info->http_host);
        lua_setfield(L, -2, "host");
    }
    if (info->http_method[0]) {
        lua_pushstring(L, info->http_method);
        lua_setfield(L, -2, "method");
    }
}

veto_lua_result veto_lua_process_packet(
    veto_lua_ctx *ctx,
    const veto_packet *pkt,
    const veto_proto_info *info,
    veto_direction dir)
{
    if (!ctx || !ctx->initialized) return make_lua_result(LUA_VERDICT_PASS);

    lua_getglobal(ctx->L, "process_packet");
    if (!lua_isfunction(ctx->L, -1)) {
        lua_pop(ctx->L, 1);
        return make_lua_result(LUA_VERDICT_PASS);
    }

    veto_lua_push_packet(ctx->L, pkt, info);
    veto_lua_push_proto_info(ctx->L, info);
    lua_pushinteger(ctx->L, dir);

    if (lua_pcall(ctx->L, 3, 1, 0) != LUA_OK) {
        fprintf(stderr, "[LUA ERROR] process_packet: %s\n", lua_tostring(ctx->L, -1));
        lua_pop(ctx->L, 1);
        return make_lua_result(LUA_VERDICT_PASS);
    }

    veto_lua_result result = make_lua_result(LUA_VERDICT_PASS);

    if (lua_istable(ctx->L, -1)) {
        lua_getfield(ctx->L, -1, "verdict");
        if (lua_isinteger(ctx->L, -1)) {
            result.verdict = (veto_lua_verdict)lua_tointeger(ctx->L, -1);
        }
        lua_pop(ctx->L, 1);

        lua_getfield(ctx->L, -1, "data");
        if (lua_isstring(ctx->L, -1)) {
            size_t len;
            const char *data = lua_tolstring(ctx->L, -1, &len);
            result.data = malloc(len);
            if (result.data) {
                memcpy(result.data, data, len);
                result.len = len;
            }
        }
        lua_pop(ctx->L, 1);

        lua_getfield(ctx->L, -1, "fake");
        if (lua_isstring(ctx->L, -1)) {
            size_t len;
            const char *data = lua_tolstring(ctx->L, -1, &len);
            result.fake_data = malloc(len);
            if (result.fake_data) {
                memcpy(result.fake_data, data, len);
                result.fake_len = len;
                result.has_fake = true;
            }
        }
        lua_pop(ctx->L, 1);
    } else if (lua_isinteger(ctx->L, -1)) {
        result.verdict = (veto_lua_verdict)lua_tointeger(ctx->L, -1);
    }

    lua_pop(ctx->L, 1);
    return result;
}
