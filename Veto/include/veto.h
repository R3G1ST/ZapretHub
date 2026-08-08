#ifndef VETO_H
#define VETO_H

#define VETO_VERSION "1.0.0"
#define VETO_NAME "Veto DPI Bypass Engine"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    VETO_OK = 0,
    VETO_ERR_NOMEM = -1,
    VETO_ERR_IO = -2,
    VETO_ERR_INVALID = -3,
    VETO_ERR_TIMEOUT = -4,
    VETO_ERR_NOTSUP = -5
} veto_status;

typedef enum {
    PROTO_UNKNOWN = 0,
    PROTO_HTTP,
    PROTO_TLS,
    PROTO_QUIC,
    PROTO_WIREGUARD,
    PROTO_DISCORD,
    PROTO_STUN,
    PROTO_DNS
} veto_proto;

typedef enum {
    DIR_OUTGOING = 0,
    DIR_INCOMING
} veto_direction;

typedef enum {
    VERDICT_PASS = 0,
    VERDICT_MODIFY,
    VERDICT_DROP,
    VERDICT_FAKE
} veto_verdict;

typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  protocol;
    veto_direction dir;
} veto_ip5tuple;

typedef struct {
    veto_verdict verdict;
    uint8_t *data;
    size_t len;
    bool own_data;
} veto_packet_result;

#endif
