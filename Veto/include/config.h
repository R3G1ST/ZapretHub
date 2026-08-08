#ifndef VETO_CONFIG_H
#define VETO_CONFIG_H

#include "veto.h"
#include "attacks.h"

typedef struct {
    char host[256];
    uint16_t port;
    veto_proto proto;
} veto_endpoint;

typedef struct {
    veto_endpoint *endpoints;
    size_t endpoint_count;
    char **domains;
    size_t domain_count;
} veto_hostlist;

typedef struct {
    char name[64];
    veto_attack_config *attacks;
    veto_hostlist *hostlist;
    bool enabled;
} veto_strategy;

typedef struct {
    char name[64];
    veto_strategy **strategies;
    size_t strategy_count;
    bool enabled;
} veto_profile;

typedef struct {
    char config_path[256];
    veto_profile **profiles;
    size_t profile_count;
    uint16_t listen_port;
    uint32_t reassembly_timeout_ms;
    size_t max_streams;
    bool verbose;
    bool daemon;
} veto_config;

veto_config *veto_config_create(void);
void veto_config_destroy(veto_config *cfg);

veto_config *veto_config_load(const char *path);
veto_status veto_config_save(const veto_config *cfg, const char *path);

veto_status veto_config_add_profile(veto_config *cfg, veto_profile *profile);
veto_status veto_config_add_strategy(veto_profile *profile, veto_strategy *strategy);

#endif
