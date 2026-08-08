#include "proto_detect.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static void *veto_memmem(const void *haystack, size_t haystack_len,
                         const void *needle, size_t needle_len) {
    if (needle_len == 0) return (void *)haystack;
    if (needle_len > haystack_len) return NULL;

    const uint8_t *h = (const uint8_t *)haystack;
    const uint8_t *n = (const uint8_t *)needle;

    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (h[i] == n[0] && memcmp(h + i, n, needle_len) == 0) {
            return (void *)(h + i);
        }
    }
    return NULL;
}

#define memmem veto_memmem

static veto_proto_info make_info(veto_proto proto) {
    veto_proto_info info;
    memset(&info, 0, sizeof(info));
    info.proto = proto;
    return info;
}

veto_proto_info veto_proto_detect_tls_client_hello(const uint8_t *data, size_t len) {
    veto_proto_info info = make_info(PROTO_TLS);
    info.is_client_hello = true;

    if (len < 5) return info;
    if (data[0] != 0x16) return info;
    if (data[1] != 0x03) return info;

    const uint8_t *p = data + 5;
    const uint8_t *end = data + len;

    if (p >= end || *p != 0x01) return info;
    p++;

    if (p + 3 > end) return info;
    p += 3;

    if (p + 2 > end) return info;
    p += 2 + 32;

    if (p >= end) return info;
    uint8_t session_id_len = *p;
    p += 1 + session_id_len;

    if (p + 2 > end) return info;
    p += 2;

    if (p + 2 > end) return info;
    uint16_t cipher_len = (p[0] << 8) | p[1];
    p += 2 + cipher_len;

    if (p >= end) return info;
    uint8_t comp_len = *p;
    p += 1 + comp_len;

    if (p + 2 > end) return info;
    uint16_t ext_len = (p[0] << 8) | p[1];
    p += 2;

    const uint8_t *ext_end = p + ext_len;
    while (p + 4 <= ext_end) {
        uint16_t ext_type = (p[0] << 8) | p[1];
        uint16_t ext_data_len = (p[2] << 8) | p[3];
        p += 4;

        if (ext_type == 0x0000) {
            if (p + 5 > ext_end) break;
            uint16_t name_len = (p[1] << 8) | p[2];
            if (name_len < 256) {
                memcpy(info.tls_sni, p + 3, name_len);
                info.tls_sni[name_len] = '\0';
                info.tls_sni_len = name_len;
            }
            break;
        }
        p += ext_data_len;
    }

    return info;
}

veto_proto_info veto_proto_detect_tls_server_hello(const uint8_t *data, size_t len) {
    veto_proto_info info = make_info(PROTO_TLS);
    info.is_server_hello = true;

    if (len < 5) return info;
    if (data[0] != 0x16) return info;
    if (data[1] != 0x03) return info;

    const uint8_t *p = data + 5;
    const uint8_t *end = data + len;

    if (p >= end || *p != 0x02) return info;

    return info;
}

veto_proto_info veto_proto_detect_http(const uint8_t *data, size_t len) {
    veto_proto_info info = make_info(PROTO_HTTP);

    if (len < 8) return info;

    static const char *methods[] = {
        "GET ", "POST ", "HEAD ", "PUT ", "DELETE ",
        "CONNECT ", "OPTIONS ", "TRACE ", "PATCH "
    };
    static const size_t method_lens[] = {
        4, 5, 5, 4, 7, 8, 8, 6, 6
    };

    for (int i = 0; i < 9; i++) {
        if (len >= method_lens[i] && memcmp(data, methods[i], method_lens[i]) == 0) {
            info.is_http_request = true;
            strcpy(info.http_method, methods[i]);
            info.http_method[method_lens[i] - 1] = '\0';

            const uint8_t *host_start = data + method_lens[i];
            const uint8_t *line_end = memmem(host_start, len - method_lens[i], "\r\n", 2);
            if (!line_end) line_end = data + len;

            const uint8_t *host_header = memmem(data, len, "Host: ", 6);
            if (!host_header) host_header = memmem(data, len, "host: ", 6);
            if (host_header && host_header < line_end) {
                host_header += 6;
                const uint8_t *host_end = memmem(host_header, line_end - host_header, "\r\n", 2);
                if (!host_end) host_end = line_end;
                size_t host_len = host_end - host_header;
                if (host_len < 256) {
                    memcpy(info.http_host, host_header, host_len);
                    info.http_host[host_len] = '\0';
                }
            }
            return info;
        }
    }

    static const char *resp_prefixes[] = { "HTTP/1.0 ", "HTTP/1.1 ", "HTTP/2 " };
    for (int i = 0; i < 3; i++) {
        size_t plen = strlen(resp_prefixes[i]);
        if (len >= plen && memcmp(data, resp_prefixes[i], plen) == 0) {
            info.is_http_response = true;
            return info;
        }
    }

    return info;
}

veto_proto_info veto_proto_detect(const uint8_t *data, size_t len) {
    if (!data || len < 5) return make_info(PROTO_UNKNOWN);

    if (data[0] == 0x16 && data[1] == 0x03 && data[2] <= 0x03) {
        const uint8_t *p = data + 5;
        if (p < data + len) {
            if (*p == 0x01) return veto_proto_detect_tls_client_hello(data, len);
            if (*p == 0x02) return veto_proto_detect_tls_server_hello(data, len);
        }
        return make_info(PROTO_TLS);
    }

    if (data[0] == 0x14 && data[1] == 0x03 && data[2] <= 0x03) {
        return make_info(PROTO_TLS);
    }

    if (data[0] == 0xFF && data[1] == 0x00 && len >= 4) {
        return make_info(PROTO_QUIC);
    }

    if (data[0] == 0xC0) {
        return make_info(PROTO_WIREGUARD);
    }

    if (data[0] == 0x12 && len >= 12) {
        return make_info(PROTO_DISCORD);
    }

    if (len >= 12 && (data[0] & 0x80) != 0) {
        uint16_t id = (data[0] << 8) | data[1];
        uint16_t flags = (data[2] << 8) | data[3];
        uint16_t qcount = (data[4] << 8) | data[5];
        if (qcount > 0 && qcount < 100) {
            return make_info(PROTO_DNS);
        }
    }

    if ((data[0] & 0xF0) == 0x40) {
        return veto_proto_detect_http(data, len);
    }

    if (len >= 8) {
        veto_proto_info http_info = veto_proto_detect_http(data, len);
        if (http_info.proto == PROTO_HTTP) return http_info;
    }

    return make_info(PROTO_UNKNOWN);
}
