/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <errno.h>
#include <string.h>

#include <zmk/split/transport/types.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)

#define ZMK_SPLIT_RELAY_EVENT_SEQUENCE_MASK 0x7F
#define ZMK_SPLIT_RELAY_EVENT_SEQUENCE_END BIT(7)
#define ZMK_SPLIT_RELAY_EVENT_ATT_VALUE_OVERHEAD 3

struct zmk_split_relay_event_chunk_reassembly_state {
    uint8_t next_sequence;
    uint16_t received_size;
    struct zmk_split_relay_event_payload payload;
};

static inline int
zmk_split_relay_event_payload_total_size(const struct zmk_split_relay_event_payload *payload,
                                         uint16_t *total_size) {
    if (payload->header.event_type_size > CONFIG_ZMK_SPLIT_RELAY_EVENT_TYPE_NAME_LEN) {
        return -EINVAL;
    }

    if (payload->header.event_data_size > CONFIG_ZMK_SPLIT_RELAY_EVENT_DATA_LEN) {
        return -EMSGSIZE;
    }

    *total_size = payload->header.event_type_size + payload->header.event_data_size;
    if (*total_size == 0) {
        return -EINVAL;
    }

    return 0;
}

static inline void
zmk_split_relay_event_copy_payload_data(const struct zmk_split_relay_event_payload *payload,
                                        uint16_t offset, uint8_t *dest, uint16_t len) {
    uint16_t event_type_size = payload->header.event_type_size;

    if (offset < event_type_size) {
        uint16_t type_len = MIN(len, event_type_size - offset);
        memcpy(dest, payload->event_type + offset, type_len);
        dest += type_len;
        len -= type_len;
        offset += type_len;
    }

    if (len > 0) {
        memcpy(dest, payload->event_data + offset - event_type_size, len);
    }
}

static inline void zmk_split_relay_event_chunk_reassembly_reset(
    struct zmk_split_relay_event_chunk_reassembly_state *state) {
    memset(state, 0, sizeof(*state));
}

static inline int zmk_split_relay_event_chunk_reassembly_accept(
    struct zmk_split_relay_event_chunk_reassembly_state *state,
    const struct relay_event_header *header, const uint8_t *data, uint16_t len) {
    uint16_t chunk_len = len;
    uint8_t sequence = header->sequence & ZMK_SPLIT_RELAY_EVENT_SEQUENCE_MASK;
    bool is_end = (header->sequence & ZMK_SPLIT_RELAY_EVENT_SEQUENCE_END) != 0;

    if (len == 0 || header->event_type_size > CONFIG_ZMK_SPLIT_RELAY_EVENT_TYPE_NAME_LEN ||
        len != header->event_data_size) {
        zmk_split_relay_event_chunk_reassembly_reset(state);
        return -EINVAL;
    }

    if (state->received_size == 0) {
        zmk_split_relay_event_chunk_reassembly_reset(state);
        state->next_sequence = (sequence + 1) & ZMK_SPLIT_RELAY_EVENT_SEQUENCE_MASK;
        state->payload.header.event_type_size = header->event_type_size;
    } else if (state->next_sequence != sequence ||
               state->payload.header.event_type_size != header->event_type_size) {
        zmk_split_relay_event_chunk_reassembly_reset(state);
        return -EINVAL;
    }

    if (state->received_size + chunk_len >
        header->event_type_size + CONFIG_ZMK_SPLIT_RELAY_EVENT_DATA_LEN) {
        zmk_split_relay_event_chunk_reassembly_reset(state);
        return -EMSGSIZE;
    }

    uint16_t offset = state->received_size;
    uint16_t event_type_size = state->payload.header.event_type_size;

    if (offset < event_type_size) {
        uint16_t type_len = MIN(len, event_type_size - offset);
        memcpy(state->payload.event_type + offset, data, type_len);
        data += type_len;
        len -= type_len;
        offset += type_len;
    }

    if (len > 0) {
        memcpy(state->payload.event_data + offset - event_type_size, data, len);
    }

    state->received_size += chunk_len;
    state->next_sequence = (sequence + 1) & ZMK_SPLIT_RELAY_EVENT_SEQUENCE_MASK;

    if (is_end) {
        if (state->received_size < state->payload.header.event_type_size) {
            zmk_split_relay_event_chunk_reassembly_reset(state);
            return -EINVAL;
        }

        state->payload.event_type[state->payload.header.event_type_size] = '\0';
        state->payload.header.event_data_size =
            state->received_size - state->payload.header.event_type_size;
        state->received_size = 0;
        return 1;
    }

    if (state->received_size == header->event_type_size + CONFIG_ZMK_SPLIT_RELAY_EVENT_DATA_LEN) {
        zmk_split_relay_event_chunk_reassembly_reset(state);
        return -EINVAL;
    }

    return 0;
}

#endif
