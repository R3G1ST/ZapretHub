#ifndef VETO_ATTACKS_H
#define VETO_ATTACKS_H

#include "veto.h"
#include "packet.h"
#include "proto_detect.h"

typedef enum {
    ATTACK_NONE = 0,
    ATTACK_FAKE,
    ATTACK_SPLIT,
    ATTACK_DISORDER,
    ATTACK_FAKED_SPLIT,
    ATTACK_HOST_FAKE_SPLIT,
    ATTACK_IP_FRAG,
    ATTACK_UDP_FRAG,
    ATTACK_DNS_HIJACK,
    ATTACK_TAMPER,
    ATTACK_RST,
    ATTACK_WSIZE
} veto_attack_type;

typedef enum {
    FOOL_NONE = 0,
    FOOL_BAD_SEQ,
    FOOL_BAD_SUM,
    FOOL_TTL,
    FOOL_TS,
    FOOL_MD5SIG
} veto_fool_method;

typedef struct {
    veto_attack_type type;
    veto_fool_method fool;
    uint16_t split_pos[8];
    uint8_t  split_count;
    uint8_t  fake_ttl;
    uint16_t fake_port;
    uint8_t *fake_payload;
    size_t   fake_payload_len;
    uint32_t start_seq;
    uint32_t cutoff_seq;
    bool     enabled;
} veto_attack_rule;

typedef struct {
    veto_attack_rule rules[16];
    uint8_t rule_count;
    char hostlist_path[256];
    char **hostlist;
    size_t hostlist_count;
} veto_attack_config;

typedef struct {
    veto_verdict verdict;
    uint8_t *packet_data;
    size_t packet_len;
    uint8_t *fake_data;
    size_t fake_len;
    bool send_fake;
} veto_attack_result;

veto_attack_config *veto_attack_config_create(void);
void veto_attack_config_destroy(veto_attack_config *cfg);

bool veto_attack_load_hostlist(veto_attack_config *cfg, const char *path);
bool veto_attack_check_hostlist(const veto_attack_config *cfg, const char *host);

veto_attack_result veto_attack_execute(
    const veto_attack_config *cfg,
    const veto_packet *pkt,
    const veto_proto_info *info
);

#endif
