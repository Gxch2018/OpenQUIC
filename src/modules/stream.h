/*
 * Copyright (c) 2020 Gscienty <gaoxiaochuan@hotmail.com>
 *
 * Distributed under the MIT software license, see the accompanying
 * file LICENSE or https://www.opensource.org/licenses/mit-license.php .
 *
 */

#ifndef __OPENQUIC_STREAM_H__
#define __OPENQUIC_STREAM_H__

#include "def.h"
#include "sorter.h"
#include "session.h"
#include "format/frame.h"
#include "recovery/flowctrl.h"
#include "utils/errno.h"
#include "utils/rbt.h"
#include "utils/link.h"
#include "liteco.h"
#include <stdint.h>
#include <pthread.h>

#define quic_stream_id_transfer(bidi, is_client, key) \
    ((bidi) ? 0 : 2) + ((is_client) ? 0 : 1) + (((key) - 1) << 2)

extern quic_module_t quic_stream_module;

#define QUIC_SEND_STREAM_FIELDS       \
    pthread_mutex_t mtx;              \
    const void *reader_buf;           \
    uint64_t reader_len;              \
    uint64_t off;                     \
                                      \
    liteco_channel_t writed_notifier; \
    liteco_channel_t *speaker;        \
    uint64_t deadline;                \
                                      \
    bool closed;                      \
                                      \

typedef struct quic_send_stream_s quic_send_stream_t;
struct quic_send_stream_s {
    QUIC_SEND_STREAM_FIELDS
};

static inline quic_err_t quic_send_stream_init(str, speaker)
    quic_send_stream_t *const str;
    liteco_channel_t *const speaker; {
    
    pthread_mutex_init(&str->mtx, NULL);
    str->reader_buf = NULL;
    str->reader_len = 0;
    str->off = 0;

    liteco_channel_init(&str->writed_notifier);
    str->speaker = speaker;
    str->deadline = 0;

    str->closed = false;

    return quic_err_success;
}

static inline quic_err_t quic_send_stream_destory(quic_send_stream_t *const str) {
    liteco_channel_close(&str->writed_notifier);
    pthread_mutex_destroy(&str->mtx);

    free(str);

    return quic_err_success;
}

static inline bool quic_send_stream_empty(quic_send_stream_t *const str) {
    pthread_mutex_lock(&str->mtx);
    bool result = str->reader_len == 0;
    pthread_mutex_unlock(&str->mtx);
    return result;
}

static inline quic_err_t quic_send_stream_set_deadline(quic_send_stream_t *const str, const uint64_t deadline) {
    pthread_mutex_lock(&str->mtx);
    str->deadline = deadline;
    pthread_mutex_unlock(&str->mtx);
    liteco_channel_notify(&str->writed_notifier);
    return quic_err_success;
}

uint64_t quic_send_stream_write(quic_send_stream_t *const str, uint64_t len, const void *data);

quic_frame_stream_t *quic_send_stream_generate(quic_send_stream_t *const str, uint64_t bytes, const bool fill);

#define QUIC_RECV_STREAM_FIELDS        \
    pthread_mutex_t mtx;               \
    quic_sorter_t sorter;              \
                                       \
    liteco_channel_t handled_notifier; \
    liteco_channel_t *speaker;         \
                                       \
    uint64_t read_off;                 \
    uint64_t final_off;                \
    bool fin_flag;                     \
                                       \
    uint64_t deadline;                 \
                                       \
    bool closed;                       \

typedef struct quic_recv_stream_s quic_recv_stream_t;
struct quic_recv_stream_s {
    QUIC_RECV_STREAM_FIELDS
};

static inline quic_err_t quic_recv_stream_init(str, speaker)
    quic_recv_stream_t *const str;
    liteco_channel_t *const speaker; {

    pthread_mutex_init(&str->mtx, NULL);
    quic_sorter_init(&str->sorter);
    liteco_channel_init(&str->handled_notifier);
    str->speaker = speaker;
    str->read_off = 0;
    str->final_off = QUIC_SORTER_MAX_SIZE;
    str->fin_flag = false;
    str->deadline = 0;
    str->closed = false;

    return quic_err_success;
}

static inline quic_err_t quic_recv_stream_destory(quic_recv_stream_t *const str) {
    liteco_channel_close(&str->handled_notifier);
    pthread_mutex_destroy(&str->mtx);
    quic_sorter_destory(&str->sorter);

    free(str);

    return quic_err_success;
}

uint64_t quic_recv_stream_read(quic_recv_stream_t *const str, const uint64_t len, void *const data);

typedef struct quic_stream_s quic_stream_t;
struct quic_stream_s {
    QUIC_RBT_UINT64_FIELDS

    quic_send_stream_t send;
    quic_recv_stream_t recv;

    quic_stream_flowctrl_module_t *flowctrl_module;
    uint8_t extends[0];
};

#define quic_container_of_send_stream(str) \
    ((quic_stream_t *) (((void *) (str)) - offsetof(quic_stream_t, send)))

#define quic_container_of_recv_stream(str) \
    ((quic_stream_t *) (((void *) (str)) - offsetof(quic_stream_t, recv)))

#define quic_streams_insert(streams, stream) \
    quic_rbt_insert((streams), (stream), quic_rbt_uint64_comparer)

#define quic_streams_find(streams, key) \
    ((quic_stream_t *) quic_rbt_find((streams), (key), quic_rbt_uint64_key_comparer))

#define quic_stream_extends(type, str) \
    ((type *) ((str)->extends + (str)->flowctrl_module->size))

#define quic_stream_extend_flowctrl(str) \
    ((void *) ((str)->extends))

static inline quic_stream_t *quic_stream_create(sid, sess, extends_size, sent_speaker, recv_speaker)
    const uint64_t sid;
    quic_session_t *const sess;
    const uint32_t extends_size;
    liteco_channel_t *const sent_speaker;
    liteco_channel_t *const recv_speaker; {

    quic_stream_flowctrl_module_t *const flowctrl_module = quic_session_stream_flowctrl(sess);

    quic_stream_t *str = malloc(sizeof(quic_stream_t) + flowctrl_module->size + extends_size);
    if (str == NULL) {
        return NULL;
    }
    quic_rbt_init(str);
    str->key = sid;
    str->flowctrl_module = flowctrl_module;
    quic_stream_flowctrl_init(str->flowctrl_module, quic_stream_extend_flowctrl(str));

    quic_send_stream_init(&str->send, sent_speaker);
    quic_recv_stream_init(&str->recv, recv_speaker);

    return str;
}

static inline quic_err_t quic_stream_destory(quic_stream_t *const str, quic_session_t *const sess) {
    quic_stream_flowctrl_module_t *const flowctrl_module = quic_session_stream_flowctrl(sess);

    quic_send_stream_destory(&str->send);
    quic_recv_stream_destory(&str->recv);
    
    if (flowctrl_module->destory) {
        flowctrl_module->destory(quic_stream_extend_flowctrl(str));
    }

    free(str);

    return quic_err_success;
}

static inline quic_err_t quic_recv_stream_handle_frame(quic_recv_stream_t *const str, quic_frame_stream_t *const frame) {
    quic_err_t err = quic_err_success;
    pthread_mutex_lock(&str->mtx);
    uint64_t t_off = frame->off + frame->len;
    bool fin = (frame->first_byte & quic_frame_stream_type_fin) == quic_frame_stream_type_fin;
    bool newly_fin = false;
    quic_stream_flowctrl_module_t *const flowctrl_module = quic_container_of_recv_stream(str)->flowctrl_module;

    flowctrl_module->update_rwnd(quic_stream_extend_flowctrl(quic_container_of_recv_stream(str)), t_off, fin);

    if (fin) {
        newly_fin = str->final_off == QUIC_SORTER_MAX_SIZE;
        str->final_off = t_off;
        str->fin_flag = true;
    }

    if (str->closed) {
        pthread_mutex_unlock(&str->mtx);
        if (newly_fin) {
            flowctrl_module->abandon(quic_stream_extend_flowctrl(quic_container_of_recv_stream(str)));
            liteco_channel_send(str->speaker, &quic_container_of_recv_stream(str)->key);
        }
        return quic_err_success;
    }

    if ((err = quic_sorter_write(&str->sorter, frame->off, frame->len, frame->data)) != quic_err_success) {
        pthread_mutex_unlock(&str->mtx);
        return quic_err_success;
    }

    pthread_mutex_unlock(&str->mtx);

    liteco_channel_notify(&str->handled_notifier);
    return quic_err_success;
}

static inline quic_err_t quic_send_stream_close(quic_send_stream_t *const str) {
    pthread_mutex_lock(&str->mtx);
    if (str->closed) {
        pthread_mutex_unlock(&str->mtx);
        return quic_err_closed;
    }
    str->closed = true;
    pthread_mutex_unlock(&str->mtx);
    liteco_channel_notify(&str->writed_notifier);
    liteco_channel_send(str->speaker, &quic_container_of_send_stream(str)->key);

    return quic_err_success;
}

#define QUIC_STREAMS_FIELDS \
    pthread_mutex_t mtx;    \
    quic_rbt_t *streams;    \
    uint32_t streams_count; \
    uint64_t next_sid;      \

#define quic_streams_basic_init(strs) {     \
    pthread_mutex_init(&(strs)->mtx, NULL); \
    quic_rbt_tree_init((strs)->streams);    \
    (strs)->streams_count = 0;              \
}

typedef struct quic_inbidi_streams_s quic_inbidi_streams_t;
struct quic_inbidi_streams_s {
    QUIC_STREAMS_FIELDS

    uint64_t next_open_sid;
    liteco_channel_t accept_speaker;
};

static inline quic_err_t quic_inbidi_streams_init(quic_inbidi_streams_t *const strs) {
    quic_streams_basic_init(strs);

    strs->next_open_sid = 1;
    strs->next_sid = 1;
    liteco_channel_init(&strs->accept_speaker);

    return quic_err_success;
}

typedef struct quic_inuni_streams_s quic_inuni_streams_t;
struct quic_inuni_streams_s {
    QUIC_STREAMS_FIELDS

    uint64_t next_open_sid;
    liteco_channel_t accept_speaker;
};

static inline quic_err_t quic_inuni_streams_init(quic_inuni_streams_t *const strs) {
    quic_streams_basic_init(strs);

    strs->next_open_sid = 1;
    strs->next_sid = 1;
    liteco_channel_init(&strs->accept_speaker);

    return quic_err_success;
}

typedef struct quic_outbidi_streams_s quic_outbidi_streams_t;
struct quic_outbidi_streams_s {
    QUIC_STREAMS_FIELDS
};

static inline quic_err_t quic_outbidi_streams_init(quic_outbidi_streams_t *const strs) {
    quic_streams_basic_init(strs);
    strs->next_sid = 1;

    return quic_err_success;
}

typedef struct quic_outuni_streams_s quic_outuni_streams_t;
struct quic_outuni_streams_s {
    QUIC_STREAMS_FIELDS
};

static inline quic_err_t quic_outuni_streams_init(quic_outuni_streams_t *const strs) {
    quic_streams_basic_init(strs);
    strs->next_sid = 1;

    return quic_err_success;
}

typedef struct quic_stream_module_s quic_stream_module_t;
struct quic_stream_module_s {
    quic_inuni_streams_t inuni;
    quic_inbidi_streams_t inbidi;
    quic_outuni_streams_t outuni;
    quic_outbidi_streams_t outbidi;

    liteco_channel_t sent_speaker;
    liteco_channel_t recv_speaker;

    uint32_t extends_size;

    quic_err_t (*init) (quic_stream_t *const str);
    void (*destory) (quic_stream_t *const str);
};

#define quic_stream_inuni_module(str) \
    ((quic_stream_module_t *) (((void *) (str)) - offsetof(quic_stream_module_t, inuni)))

#define quic_stream_inbidi_module(str) \
    ((quic_stream_module_t *) (((void *) (str)) - offsetof(quic_stream_module_t, inbidi)))

#define quic_stream_outuni_module(str) \
    ((quic_stream_module_t *) (((void *) (str)) - offsetof(quic_stream_module_t, outuni)))

#define quic_stream_outbidi_module(str) \
    ((quic_stream_module_t *) (((void *) (str)) - offsetof(quic_stream_module_t, outbidi)))

#define quic_streams_open(strs, container_of_module, stream) {                                                    \
    quic_stream_module_t *const module = container_of_module(strs);                                               \
    quic_session_t *const sess = quic_module_of_session(module, &quic_stream_module);                             \
    pthread_mutex_lock(&strs->mtx);                                                                               \
    const uint64_t sid = quic_stream_id_transfer(false, sess->is_cli, strs->next_sid);                            \
    strs->next_sid++;                                                                                             \
    stream = quic_streams_find(strs->streams, sid);                                                               \
    if (!quic_rbt_is_nil(stream)) {                                                                               \
        quic_rbt_remove(&strs->streams, &stream);                                                                 \
        if (module->destory) {                                                                                    \
            module->destory(stream);                                                                              \
        }                                                                                                         \
        quic_stream_destory(stream, sess);                                                                        \
    }                                                                                                             \
    (stream) = quic_stream_create(sid, sess, module->extends_size, &module->sent_speaker, &module->recv_speaker); \
    if (module->init) {                                                                                           \
        module->init(stream);                                                                                     \
    }                                                                                                             \
    quic_streams_insert(&strs->streams, stream);                                                                  \
    pthread_mutex_unlock(&strs->mtx);                                                                             \
}

#define quic_streams_delete(strs, container_of_module, sid) {                             \
    quic_stream_module_t *const module = container_of_module(strs);                       \
    pthread_mutex_lock(&strs->mtx);                                                       \
    quic_stream_t *stream = quic_streams_find(strs->streams, sid);                        \
    if (!quic_rbt_is_nil(stream)) {                                                       \
        quic_rbt_remove(&strs->streams, &stream);                                         \
        if (module->destory) {                                                            \
            module->destory(stream);                                                      \
        }                                                                                 \
        quic_stream_destory(stream, quic_module_of_session(module, &quic_stream_module)); \
    }                                                                                     \
    pthread_mutex_unlock(&strs->mtx);                                                     \
}

static inline quic_stream_t *quic_stream_outuni_open(quic_outuni_streams_t *const strs) {
    quic_stream_t *stream = NULL;
    quic_streams_open(strs, quic_stream_outbidi_module, stream);
    return stream;
}

static inline quic_stream_t *quic_stream_outbidi_open(quic_outbidi_streams_t *const strs) {
    quic_stream_t *stream = NULL;
    quic_streams_open(strs, quic_stream_outbidi_module, stream);
    return stream;
}

#define quic_stream_outuni_delete(strs, sid) \
    quic_streams_delete((strs), quic_stream_outuni_module, (sid))

#define quic_stream_outbidi_delete(strs, sid) \
    quic_streams_delete((strs), quic_stream_outbidi_module, (sid))


#endif