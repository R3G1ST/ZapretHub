#include "tcp_reassembly.h"
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

static uint64_t stream_id(const veto_ip5tuple *t) {
    uint64_t id = 0;
    id = (uint64_t)t->src_ip << 32 | t->dst_ip;
    id ^= (uint64_t)t->src_port << 16 | t->dst_port;
    id ^= t->protocol;
    return id;
}

veto_reassembler *veto_reassembler_create(size_t max_streams, uint32_t timeout_ms) {
    veto_reassembler *ra = calloc(1, sizeof(veto_reassembler));
    if (!ra) return NULL;
    ra->max_streams = max_streams;
    ra->stream_timeout_ms = timeout_ms;
    return ra;
}

static void free_frags(tcp_frag *f) {
    while (f) {
        tcp_frag *next = f->next;
        free(f->data);
        free(f);
        f = next;
    }
}

void veto_reassembler_destroy(veto_reassembler *ra) {
    if (!ra) return;
    tcp_stream *s = ra->streams;
    while (s) {
        tcp_stream *next = s->next;
        free_frags(s->fragments);
        free(s);
        s = next;
    }
    free(ra);
}

static tcp_stream *find_or_create_stream(veto_reassembler *ra, const veto_ip5tuple *tuple) {
    uint64_t id = stream_id(tuple);
    tcp_stream *s = ra->streams;

    while (s) {
        if (s->id == id) return s;
        s = s->next;
    }

    if (ra->stream_count >= ra->max_streams) return NULL;

    s = calloc(1, sizeof(tcp_stream));
    if (!s) return NULL;
    s->id = id;
    memcpy(&s->tuple, tuple, sizeof(veto_ip5tuple));
    s->next = ra->streams;
    ra->streams = s;
    ra->stream_count++;
    return s;
}

static void insert_frag(tcp_stream *s, uint32_t seq, const uint8_t *data, size_t len) {
    tcp_frag *f = malloc(sizeof(tcp_frag));
    if (!f) return;
    f->seq = seq;
    f->data = malloc(len);
    if (!f->data) { free(f); return; }
    memcpy(f->data, data, len);
    f->len = len;
    f->next = NULL;

    if (!s->fragments || seq < s->fragments->seq) {
        f->next = s->fragments;
        s->fragments = f;
    } else {
        tcp_frag *prev = s->fragments;
        while (prev->next && prev->next->seq < seq) {
            prev = prev->next;
        }
        f->next = prev->next;
        prev->next = f;
    }
    s->total_pending += len;
}

static bool try_reassemble(tcp_stream *s, uint8_t **out, size_t *out_len) {
    if (!s->fragments) return false;
    if (s->fragments->seq != s->expected_seq) return false;

    size_t total = s->total_pending;
    uint8_t *buf = malloc(total);
    if (!buf) return false;

    size_t offset = 0;
    tcp_frag *f = s->fragments;
    while (f && offset < total) {
        if (f->seq != s->expected_seq) {
            free(buf);
            return false;
        }
        memcpy(buf + offset, f->data, f->len);
        offset += f->len;
        s->expected_seq += f->len;
        f = f->next;
    }

    *out = buf;
    *out_len = offset;
    free_frags(s->fragments);
    s->fragments = NULL;
    s->total_pending = 0;
    return true;
}

veto_reassembly_result veto_reassembler_process(
    veto_reassembler *ra,
    const veto_packet *pkt,
    uint8_t **reassembled,
    size_t *reassembled_len)
{
    if (!ra || !pkt || !pkt->is_valid || pkt->meta.protocol != IPPROTO_TCP) {
        return REASSEMBLY_RESET;
    }

    tcp_stream *s = find_or_create_stream(ra, &pkt->meta);
    if (!s) return REASSEMBLY_RESET;

    bool is_syn = pkt->tcp_flags & 0x02;
    bool is_fin = pkt->tcp_flags & 0x01;
    bool is_rst = pkt->tcp_flags & 0x04;

    if (is_rst) {
        veto_reassembler_reset_stream(ra, &pkt->meta);
        return REASSEMBLY_RESET;
    }

    if (is_syn) {
        s->syn_seen = true;
        s->expected_seq = ntohl(pkt->tcp_seq) + 1;
        s->fin_seen = false;
        free_frags(s->fragments);
        s->fragments = NULL;
        s->total_pending = 0;
        return REASSEMBLY_PARTIAL;
    }

    if (!s->syn_seen) return REASSEMBLY_PARTIAL;

    if (is_fin) {
        s->fin_seen = true;
    }

    if (pkt->data && pkt->len > 0) {
        insert_frag(s, ntohl(pkt->tcp_seq), pkt->data, pkt->len);
    }

    if (try_reassemble(s, reassembled, reassembled_len)) {
        if (s->fin_seen) {
            veto_reassembler_reset_stream(ra, &pkt->meta);
        }
        return REASSEMBLY_COMPLETE;
    }

    return REASSEMBLY_PARTIAL;
}

void veto_reassembler_reset_stream(veto_reassembler *ra, const veto_ip5tuple *tuple) {
    if (!ra || !tuple) return;
    uint64_t id = stream_id(tuple);
    tcp_stream *prev = NULL;
    tcp_stream *s = ra->streams;

    while (s) {
        if (s->id == id) {
            if (prev) prev->next = s->next;
            else ra->streams = s->next;
            free_frags(s->fragments);
            free(s);
            ra->stream_count--;
            return;
        }
        prev = s;
        s = s->next;
    }
}
