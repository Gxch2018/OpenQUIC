/*
 * Copyright (c) 2020 Gscienty <gaoxiaochuan@hotmail.com>
 *
 * Distributed under the MIT software license, see the accompanying
 * file LICENSE or https://www.opensource.org/licenses/mit-license.php .
 *
 */

#include "parse_packet.h"
#include "format/header.h"
#include "utils/errno.h"

static quic_err_t quic_process_packet(quic_session_t *const sess, const quic_payload_t *payload);

quic_err_t quic_handle_packet(quic_session_t *const sess, const quic_buf_t buf, struct timeval recv_time) {
    union {
        quic_initial_header_t initial;
        quic_handshake_header_t handshake;
        quic_0rtt_header_t zero_rtt;
        quic_payload_t short_payload;
    } payload;

    quic_header_t *header = buf.pos;
    if (sess->is_cli && !sess->recv_first && quic_header_is_long(header)) {
        quic_buf_t src = quic_long_header_src_conn(header);
        quic_buf_setpl(&src);

        if (quic_buf_cmp(&src, &sess->handshake_dst) != 0) {
            quic_buf_copy(&sess->handshake_dst, &src);
        }
    }

    sess->recv_first = true;
    sess->last_recv_time = recv_time;

    if (quic_header_is_long(header)) {
        switch (quic_packet_type(header)) {
        case quic_packet_initial_type:
            payload.initial = quic_initial_header(header);
            break;

        case quic_packet_handshake_type:
            payload.handshake = quic_handshake_header(header);
            break;

        case quic_packet_0rtt_type:
            payload.zero_rtt = quic_0rtt_header(header);
            break;

        case quic_packet_retry_type:
            return quic_err_not_implemented;
        }
    }
    else {
        payload.short_payload = quic_short_header(header, sess->conn_len);
        payload.short_payload.payload_len = quic_buf_size(&buf) - (payload.short_payload.payload - buf.pos);
    }

    return quic_process_packet(sess, (quic_payload_t *) &payload);
}

static quic_err_t quic_process_packet(quic_session_t *const sess, const quic_payload_t *payload) {

    return quic_err_success;
}
