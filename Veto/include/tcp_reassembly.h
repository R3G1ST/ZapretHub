#ifndef VETO_TCP_REASSEMBLY_H
#define VETO_TCP_REASSEMBLY_H

#include "veto.h"
#include "packet.h"

typedef struct tcp_frag_s {
    uint32_t seq;
    uint8_t *data;
    size_t   len;
    struct tcp_frag_s *next;
} tcp_frag;

typedef struct tcp_stream_s {
    uint64_t id;
    veto_ip5tuple tuple;
    tcp_frag *fragments;
    size_t total_pending;
    uint32_t expected_seq;
    bool syn_seen;
    bool fin_seen;
    struct tcp_stream_s *next;
} tcp_stream;

typedef struct {
    tcp_stream *streams;
    size_t stream_count;
    size_t max_streams;
    uint32_t stream_timeout_ms;
} veto_reassembler;

veto_reassembler *veto_reassembler_create(size_t max_streams, uint32_t timeout_ms);
void veto_reassembler_destroy(veto_reassembler *ra);

typedef enum {
    REASSEMBLY_COMPLETE,
    REASSEMBLY_PARTIAL,
    REASSEMBLY_RESET
} veto_reassembly_result;

veto_reassembly_result veto_reassembler_process(
    veto_reassembler *ra,
    const veto_packet *pkt,
    uint8_t **reassembled,
    size_t *reassembled_len
);

void veto_reassembler_reset_stream(veto_reassembler *ra, const veto_ip5tuple *tuple);

#endif
