#ifndef VETO_PROTO_DETECT_H
#define VETO_PROTO_DETECT_H

#include "veto.h"
#include "packet.h"

typedef struct {
    veto_proto proto;
    const char *name;
    bool is_client_hello;
    bool is_server_hello;
    bool is_http_request;
    bool is_http_response;
    uint16_t tls_sni_len;
    char tls_sni[256];
    char http_host[256];
    char http_method[16];
} veto_proto_info;

veto_proto_info veto_proto_detect(const uint8_t *data, size_t len);

veto_proto_info veto_proto_detect_tls_client_hello(const uint8_t *data, size_t len);
veto_proto_info veto_proto_detect_http(const uint8_t *data, size_t len);

#endif
