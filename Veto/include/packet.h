#ifndef VETO_PACKET_H
#define VETO_PACKET_H

#include "veto.h"
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma pack(push, 1)

typedef struct {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} veto_ipv4_hdr;

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} veto_tcp_hdr;

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} veto_udp_hdr;

#pragma pack(pop)

typedef struct {
    uint8_t *data;
    size_t   len;
    veto_ip5tuple meta;
    uint32_t tcp_seq;
    uint32_t tcp_ack;
    uint16_t tcp_win;
    uint8_t  tcp_flags;
    bool     is_valid;
    uint8_t *raw_ip;
    size_t   raw_ip_len;
} veto_packet;

veto_packet *veto_packet_parse(const uint8_t *raw, size_t raw_len);
void veto_packet_free(veto_packet *pkt);

uint16_t veto_checksum_tcp(const veto_ipv4_hdr *ip, const veto_tcp_hdr *tcp,
                           const uint8_t *payload, size_t payload_len);
uint16_t veto_checksum_ip(const veto_ipv4_hdr *ip);

#endif
