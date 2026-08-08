#include "packet.h"
#include <stdlib.h>
#include <string.h>

static uint16_t calc_checksum(const uint16_t *buf, size_t len) {
    uint32_t sum = 0;
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const uint8_t *)buf;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (uint16_t)~sum;
}

uint16_t veto_checksum_ip(const veto_ipv4_hdr *ip) {
    return calc_checksum((const uint16_t *)ip, ip->ver_ihl & 0x0F);
}

uint16_t veto_checksum_tcp(const veto_ipv4_hdr *ip, const veto_tcp_hdr *tcp,
                           const uint8_t *payload, size_t payload_len) {
    uint32_t sum = 0;
    uint16_t tcp_len = ((tcp->data_offset >> 4) * 4) + payload_len;
    sum += ip->src_ip;
    sum += ip->dst_ip;
    sum += htons(IPPROTO_TCP);
    sum += htons(tcp_len);

    const uint16_t *tcp_raw = (const uint16_t *)tcp;
    size_t tcp_hdr_len = (tcp->data_offset >> 4) * 4;
    for (size_t i = 0; i < tcp_hdr_len / 2; i++) {
        sum += tcp_raw[i];
    }

    for (size_t i = 0; i < payload_len / 2; i++) {
        sum += ((const uint16_t *)payload)[i];
    }
    if (payload_len & 1) {
        sum += ((const uint8_t *)payload)[payload_len - 1] << 8;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

veto_packet *veto_packet_parse(const uint8_t *raw, size_t raw_len) {
    if (!raw || raw_len < sizeof(veto_ipv4_hdr)) return NULL;

    const veto_ipv4_hdr *ip = (const veto_ipv4_hdr *)raw;
    uint8_t ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
    if (ip_hdr_len < 20 || raw_len < ip_hdr_len) return NULL;

    veto_packet *pkt = calloc(1, sizeof(veto_packet));
    if (!pkt) return NULL;

    pkt->raw_ip = malloc(raw_len);
    if (!pkt->raw_ip) {
        free(pkt);
        return NULL;
    }
    memcpy(pkt->raw_ip, raw, raw_len);
    pkt->raw_ip_len = raw_len;

    pkt->meta.src_ip = ip->src_ip;
    pkt->meta.dst_ip = ip->dst_ip;
    pkt->meta.protocol = ip->proto;

    if (ip->proto == IPPROTO_TCP) {
        if (raw_len < ip_hdr_len + sizeof(veto_tcp_hdr)) {
            free(pkt->raw_ip);
            free(pkt);
            return NULL;
        }
        const veto_tcp_hdr *tcp = (const veto_tcp_hdr *)(raw + ip_hdr_len);
        pkt->meta.src_port = tcp->src_port;
        pkt->meta.dst_port = tcp->dst_port;
        pkt->tcp_seq = tcp->seq;
        pkt->tcp_ack = tcp->ack;
        pkt->tcp_win = tcp->window;
        pkt->tcp_flags = tcp->flags;

        uint8_t tcp_hdr_len = (tcp->data_offset >> 4) * 4;
        size_t payload_offset = ip_hdr_len + tcp_hdr_len;
        size_t payload_len = raw_len - payload_offset;

        if (payload_len > 0) {
            pkt->data = malloc(payload_len);
            if (!pkt->data) {
                free(pkt->raw_ip);
                free(pkt);
                return NULL;
            }
            memcpy(pkt->data, raw + payload_offset, payload_len);
            pkt->len = payload_len;
        }
    } else if (ip->proto == IPPROTO_UDP) {
        if (raw_len < ip_hdr_len + sizeof(veto_udp_hdr)) {
            free(pkt->raw_ip);
            free(pkt);
            return NULL;
        }
        const veto_udp_hdr *udp = (const veto_udp_hdr *)(raw + ip_hdr_len);
        pkt->meta.src_port = udp->src_port;
        pkt->meta.dst_port = udp->dst_port;

        size_t payload_offset = ip_hdr_len + sizeof(veto_udp_hdr);
        size_t payload_len = raw_len - payload_offset;

        if (payload_len > 0) {
            pkt->data = malloc(payload_len);
            if (!pkt->data) {
                free(pkt->raw_ip);
                free(pkt);
                return NULL;
            }
            memcpy(pkt->data, raw + payload_offset, payload_len);
            pkt->len = payload_len;
        }
    }

    pkt->is_valid = true;
    return pkt;
}

void veto_packet_free(veto_packet *pkt) {
    if (pkt) {
        free(pkt->data);
        free(pkt->raw_ip);
        free(pkt);
    }
}
