#include "attacks.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <winsock2.h>

veto_attack_config *veto_attack_config_create(void) {
    return calloc(1, sizeof(veto_attack_config));
}

void veto_attack_config_destroy(veto_attack_config *cfg) {
    if (!cfg) return;
    for (int i = 0; i < cfg->rule_count; i++) {
        free(cfg->rules[i].fake_payload);
    }
    if (cfg->hostlist) {
        for (size_t i = 0; i < cfg->hostlist_count; i++) {
            free(cfg->hostlist[i]);
        }
        free(cfg->hostlist);
    }
    free(cfg);
}

bool veto_attack_load_hostlist(veto_attack_config *cfg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[256];
    size_t count = 0;
    char **list = NULL;

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (len == 0 || line[0] == '#') continue;

        char **new_list = realloc(list, (count + 1) * sizeof(char *));
        if (!new_list) { free(list); fclose(f); return false; }
        list = new_list;
        list[count] = strdup(line);
        count++;
    }
    fclose(f);

    cfg->hostlist = list;
    cfg->hostlist_count = count;
    return true;
}

bool veto_attack_check_hostlist(const veto_attack_config *cfg, const char *host) {
    if (!cfg || !host || !cfg->hostlist) return false;

    for (size_t i = 0; i < cfg->hostlist_count; i++) {
        const char *pattern = cfg->hostlist[i];
        if (pattern[0] == '^') {
            if (strcmp(host, pattern + 1) == 0) return true;
        } else {
            if (strstr(host, pattern) != NULL) return true;
        }
    }
    return false;
}

static veto_attack_result make_result(veto_verdict v) {
    veto_attack_result r;
    memset(&r, 0, sizeof(r));
    r.verdict = v;
    return r;
}

static veto_attack_result execute_fake(
    const veto_attack_rule *rule,
    const veto_packet *pkt,
    const veto_proto_info *info)
{
    if (!pkt->raw_ip || pkt->raw_ip_len == 0) return make_result(VERDICT_PASS);

    veto_attack_result result = make_result(VERDICT_FAKE);

    result.fake_len = pkt->raw_ip_len;
    result.fake_data = malloc(result.fake_len);
    if (!result.fake_data) return make_result(VERDICT_PASS);
    memcpy(result.fake_data, pkt->raw_ip, result.fake_len);

    if (rule->fool == FOOL_TTL && rule->fake_ttl > 0) {
        if (result.fake_len >= sizeof(veto_ipv4_hdr)) {
            veto_ipv4_hdr *ip = (veto_ipv4_hdr *)result.fake_data;
            ip->ttl = rule->fake_ttl;
            ip->checksum = 0;
            ip->checksum = htons(veto_checksum_ip(ip));
        }
    }

    result.send_fake = true;
    return result;
}

static veto_attack_result execute_split(
    const veto_attack_rule *rule,
    const veto_packet *pkt)
{
    if (!pkt->data || pkt->len == 0) return make_result(VERDICT_PASS);

    uint16_t split_pos = rule->split_pos[0];
    if (split_pos >= pkt->len) split_pos = pkt->len / 2;

    veto_attack_result result = make_result(VERDICT_MODIFY);
    result.packet_len = pkt->len;
    result.packet_data = malloc(result.packet_len);
    if (!result.packet_data) return make_result(VERDICT_PASS);
    memcpy(result.packet_data, pkt->data, result.packet_len);

    return result;
}

static veto_attack_result execute_disorder(
    const veto_attack_rule *rule,
    const veto_packet *pkt)
{
    if (!pkt->data || pkt->len == 0) return make_result(VERDICT_PASS);

    veto_attack_result result = make_result(VERDICT_MODIFY);
    result.packet_len = pkt->len;
    result.packet_data = malloc(result.packet_len);
    if (!result.packet_data) return make_result(VERDICT_PASS);

    memcpy(result.packet_data, pkt->data, result.packet_len);

    uint16_t pos1 = rule->split_pos[0];
    uint16_t pos2 = rule->split_pos[1];
    if (pos1 < result.packet_len && pos2 < result.packet_len && pos1 < pos2) {
        size_t frag_len = pos2 - pos1;
        uint8_t tmp[1024];
        if (frag_len <= sizeof(tmp)) {
            memcpy(tmp, result.packet_data + pos1, frag_len);
            memmove(result.packet_data + pos1, result.packet_data + pos2,
                    result.packet_len - pos2);
            memcpy(result.packet_data + result.packet_len - frag_len, tmp, frag_len);
        }
    }

    return result;
}

static veto_attack_result execute_udp_frag(
    const veto_attack_rule *rule,
    const veto_packet *pkt)
{
    if (pkt->meta.protocol != IPPROTO_UDP) return make_result(VERDICT_PASS);
    if (!pkt->data || pkt->len == 0) return make_result(VERDICT_PASS);

    uint16_t frag_size = rule->split_pos[0];
    if (frag_size == 0) frag_size = 128;

    veto_attack_result result = make_result(VERDICT_MODIFY);
    result.packet_len = pkt->len;
    result.packet_data = malloc(result.packet_len);
    if (!result.packet_data) return make_result(VERDICT_PASS);
    memcpy(result.packet_data, pkt->data, result.packet_len);

    return result;
}

static veto_attack_result execute_dns_hijack(
    const veto_attack_rule *rule,
    const veto_packet *pkt)
{
    if (pkt->meta.protocol != IPPROTO_UDP) return make_result(VERDICT_PASS);
    if (pkt->meta.dst_port != htons(53)) return make_result(VERDICT_PASS);

    return make_result(VERDICT_MODIFY);
}

static veto_attack_result execute_ip_frag(
    const veto_attack_rule *rule,
    const veto_packet *pkt)
{
    if (!pkt->data || pkt->len < sizeof(veto_ipv4_hdr)) return make_result(VERDICT_PASS);

    uint16_t frag_size = rule->split_pos[0];
    if (frag_size == 0) frag_size = 64;

    veto_attack_result result = make_result(VERDICT_MODIFY);
    result.packet_len = pkt->len;
    result.packet_data = malloc(result.packet_len);
    if (!result.packet_data) return make_result(VERDICT_PASS);
    memcpy(result.packet_data, pkt->data, result.packet_len);

    veto_ipv4_hdr *ip = (veto_ipv4_hdr *)result.packet_data;
    uint16_t total_len = ntohs(ip->total_len);

    if (total_len > frag_size + sizeof(veto_ipv4_hdr)) {
        ip->flags_frag = htons(0x2000 | (frag_size >> 3));
        ip->checksum = 0;
        ip->checksum = htons(veto_checksum_ip(ip));
    }

    return result;
}

static veto_attack_result execute_tamper(
    const veto_attack_rule *rule,
    const veto_packet *pkt)
{
    if (!pkt->data || pkt->len < sizeof(veto_ipv4_hdr) + sizeof(veto_tcp_hdr))
        return make_result(VERDICT_PASS);

    veto_attack_result result = make_result(VERDICT_MODIFY);
    result.packet_len = pkt->len;
    result.packet_data = malloc(result.packet_len);
    if (!result.packet_data) return make_result(VERDICT_PASS);
    memcpy(result.packet_data, pkt->data, result.packet_len);

    uint8_t ip_hdr_len = ((veto_ipv4_hdr *)result.packet_data)->ver_ihl & 0x0F;
    if (ip_hdr_len * 4 + sizeof(veto_tcp_hdr) > result.packet_len)
        return result;

    veto_tcp_hdr *tcp = (veto_tcp_hdr *)(result.packet_data + ip_hdr_len * 4);

    uint8_t tcp_hdr_len = (tcp->data_offset >> 4) * 4;
    size_t payload_offset = ip_hdr_len * 4 + tcp_hdr_len;
    size_t payload_len = result.packet_len - payload_offset;

    if (payload_len > 0 && rule->fool == FOOL_BAD_SUM) {
        uint8_t *payload = result.packet_data + payload_offset;
        if (payload_len > 0) {
            payload[0] ^= 0xFF;
        }
    }

    return result;
}

static veto_attack_result execute_rst(
    const veto_attack_rule *rule,
    const veto_packet *pkt)
{
    if (pkt->meta.protocol != IPPROTO_TCP) return make_result(VERDICT_PASS);
    if (!pkt->data || pkt->len < sizeof(veto_ipv4_hdr) + sizeof(veto_tcp_hdr))
        return make_result(VERDICT_PASS);

    uint8_t flags = pkt->tcp_flags;
    if ((flags & 0x04) != 0) return make_result(VERDICT_PASS);

    veto_attack_result result = make_result(VERDICT_MODIFY);
    result.packet_len = pkt->len;
    result.packet_data = malloc(result.packet_len);
    if (!result.packet_data) return make_result(VERDICT_PASS);
    memcpy(result.packet_data, pkt->data, result.packet_len);

    uint8_t ip_hdr_len = ((veto_ipv4_hdr *)result.packet_data)->ver_ihl & 0x0F;
    if (ip_hdr_len * 4 + sizeof(veto_tcp_hdr) > result.packet_len)
        return result;

    veto_tcp_hdr *tcp = (veto_tcp_hdr *)(result.packet_data + ip_hdr_len * 4);
    tcp->flags = 0x14;
    tcp->checksum = 0;

    veto_ipv4_hdr *ip = (veto_ipv4_hdr *)result.packet_data;
    uint16_t tcp_len = ((tcp->data_offset >> 4) * 4);
    ip->checksum = 0;
    ip->checksum = htons(veto_checksum_ip(ip));

    return result;
}

static veto_attack_result execute_wsize(
    const veto_attack_rule *rule,
    const veto_packet *pkt)
{
    if (pkt->meta.protocol != IPPROTO_TCP) return make_result(VERDICT_PASS);
    if (!pkt->data || pkt->len < sizeof(veto_ipv4_hdr) + sizeof(veto_tcp_hdr))
        return make_result(VERDICT_PASS);

    uint16_t new_window = rule->fake_port;
    if (new_window == 0) new_window = 1024;

    veto_attack_result result = make_result(VERDICT_MODIFY);
    result.packet_len = pkt->len;
    result.packet_data = malloc(result.packet_len);
    if (!result.packet_data) return make_result(VERDICT_PASS);
    memcpy(result.packet_data, pkt->data, result.packet_len);

    uint8_t ip_hdr_len = ((veto_ipv4_hdr *)result.packet_data)->ver_ihl & 0x0F;
    if (ip_hdr_len * 4 + sizeof(veto_tcp_hdr) > result.packet_len)
        return result;

    veto_tcp_hdr *tcp = (veto_tcp_hdr *)(result.packet_data + ip_hdr_len * 4);
    tcp->window = htons(new_window);
    tcp->checksum = 0;

    veto_ipv4_hdr *ip = (veto_ipv4_hdr *)result.packet_data;
    uint16_t tcp_len = ((tcp->data_offset >> 4) * 4);
    size_t payload_len = result.packet_len - ip_hdr_len * 4 - tcp_len;

    uint32_t sum = 0;
    sum += ip->src_ip;
    sum += ip->dst_ip;
    sum += htons(IPPROTO_TCP);
    sum += htons(tcp_len + payload_len);

    const uint16_t *tcp_raw = (const uint16_t *)tcp;
    size_t tcp_full_len = tcp_len + payload_len;
    for (size_t i = 0; i < tcp_full_len / 2; i++) {
        sum += tcp_raw[i];
    }
    if (tcp_full_len & 1) {
        sum += ((const uint8_t *)tcp_raw)[tcp_full_len - 1] << 8;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    tcp->checksum = htons((uint16_t)~sum);

    ip->checksum = 0;
    ip->checksum = htons(veto_checksum_ip(ip));

    return result;
}

veto_attack_result veto_attack_execute(
    const veto_attack_config *cfg,
    const veto_packet *pkt,
    const veto_proto_info *info)
{
    if (!cfg || !pkt) return make_result(VERDICT_PASS);

    const char *host = NULL;
    if (info->proto == PROTO_TLS && info->tls_sni[0]) {
        host = info->tls_sni;
    } else if (info->proto == PROTO_HTTP && info->http_host[0]) {
        host = info->http_host;
    }

    if (host && cfg->hostlist) {
        if (!veto_attack_check_hostlist(cfg, host)) {
            return make_result(VERDICT_PASS);
        }
    }

    bool is_udp = (pkt->meta.protocol == IPPROTO_UDP);
    bool is_tcp = (pkt->meta.protocol == IPPROTO_TCP);

    for (int i = 0; i < cfg->rule_count; i++) {
        if (!cfg->rules[i].enabled) continue;

        bool proto_match = false;
        if (is_tcp && info->proto != PROTO_UNKNOWN) {
            proto_match = true;
        }
        if (is_udp) {
            if (cfg->rules[i].type == ATTACK_UDP_FRAG ||
                cfg->rules[i].type == ATTACK_DNS_HIJACK) {
                proto_match = true;
            }
        }
        if (!proto_match) continue;

        switch (cfg->rules[i].type) {
            case ATTACK_FAKE:
                return execute_fake(&cfg->rules[i], pkt, info);
            case ATTACK_SPLIT:
                return execute_split(&cfg->rules[i], pkt);
            case ATTACK_DISORDER:
                return execute_disorder(&cfg->rules[i], pkt);
            case ATTACK_IP_FRAG:
                return execute_ip_frag(&cfg->rules[i], pkt);
            case ATTACK_UDP_FRAG:
                return execute_udp_frag(&cfg->rules[i], pkt);
            case ATTACK_DNS_HIJACK:
                return execute_dns_hijack(&cfg->rules[i], pkt);
            case ATTACK_TAMPER:
                return execute_tamper(&cfg->rules[i], pkt);
            case ATTACK_RST:
                return execute_rst(&cfg->rules[i], pkt);
            case ATTACK_WSIZE:
                return execute_wsize(&cfg->rules[i], pkt);
            default:
                break;
        }
    }

    return make_result(VERDICT_PASS);
}
