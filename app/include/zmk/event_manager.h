/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>

struct zmk_event_type {
    const char *name;
};

typedef struct {
    const struct zmk_event_type *event;
    uint8_t last_listener_index;
} zmk_event_t;

#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_EV_EVENT_HANDLED 1
#define ZMK_EV_EVENT_CAPTURED 2

typedef int (*zmk_listener_callback_t)(const zmk_event_t *eh);
struct zmk_listener {
    zmk_listener_callback_t callback;
};

struct zmk_event_subscription {
    const struct zmk_event_type *event_type;
    const struct zmk_listener *listener;
};

#define ZMK_EVENT_DECLARE(event_type)                                                              \
    struct event_type##_event {                                                                    \
        zmk_event_t header;                                                                        \
        struct event_type data;                                                                    \
    };                                                                                             \
    struct event_type##_event copy_raised_##event_type(const struct event_type *ev);               \
    int raise_##event_type(struct event_type);                                                     \
    struct event_type *as_##event_type(const zmk_event_t *eh);                                     \
    extern const struct zmk_event_type zmk_event_##event_type;

#define ZMK_EVENT_IMPL(event_type)                                                                 \
    const struct zmk_event_type zmk_event_##event_type = {.name = STRINGIFY(event_type)};          \
    const struct zmk_event_type *zmk_event_ref_##event_type __used                                 \
        __attribute__((__section__(".event_type"))) = &zmk_event_##event_type;                     \
    struct event_type##_event copy_raised_##event_type(const struct event_type *ev) {              \
        struct event_type##_event *outer = CONTAINER_OF(ev, struct event_type##_event, data);      \
        return *outer;                                                                             \
    };                                                                                             \
    int raise_##event_type(struct event_type data) {                                               \
        struct event_type##_event ev = {.data = data,                                              \
                                        .header = {.event = &zmk_event_##event_type}};             \
        return ZMK_EVENT_RAISE(ev);                                                                \
    };                                                                                             \
    struct event_type *as_##event_type(const zmk_event_t *eh) {                                    \
        return (eh->event == &zmk_event_##event_type) ? &((struct event_type##_event *)eh)->data   \
                                                      : NULL;                                      \
    };

#define ZMK_LISTENER(mod, cb) const struct zmk_listener zmk_listener_##mod = {.callback = cb};

#define ZMK_SUBSCRIPTION(mod, ev_type)                                                             \
    extern const struct zmk_listener zmk_listener_##mod;                                           \
    const Z_DECL_ALIGN(struct zmk_event_subscription)                                              \
        _CONCAT(_CONCAT(zmk_event_sub_, mod), ev_type) __used                                      \
        __attribute__((__section__(".event_subscription"))) = {                                    \
            .event_type = &zmk_event_##ev_type,                                                    \
            .listener = &zmk_listener_##mod,                                                       \
    };

#define ZMK_EVENT_RAISE(ev) zmk_event_manager_raise(&(ev).header)

#define ZMK_EVENT_RAISE_AFTER(ev, mod)                                                             \
    zmk_event_manager_raise_after(&(ev).header, &zmk_listener_##mod)

#define ZMK_EVENT_RAISE_AT(ev, mod) zmk_event_manager_raise_at(&(ev).header, &zmk_listener_##mod)

#define ZMK_EVENT_RELEASE(ev) zmk_event_manager_release(&(ev).header)

int zmk_event_manager_raise(zmk_event_t *event);
int zmk_event_manager_raise_after(zmk_event_t *event, const struct zmk_listener *listener);
int zmk_event_manager_raise_at(zmk_event_t *event, const struct zmk_listener *listener);
int zmk_event_manager_release(zmk_event_t *event);

// indicates that the relay event originated from self
#define ZMK_RELAY_EVENT_SOURCE_SELF 0xFF

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)

#define __ZMK_RELAY_ASSERT_NAME(identifier)                                                        \
    BUILD_ASSERT(sizeof(#identifier) <= CONFIG_ZMK_SPLIT_RELAY_EVENT_TYPE_NAME_LEN,                \
                 "Name " STRINGIFY(identifier) " too large for relay event");

#define __ZMK_RELAY_ASSERT_SIZE(event_type, identifier)                                            \
    BUILD_ASSERT(sizeof(struct event_type) <= CONFIG_ZMK_SPLIT_RELAY_EVENT_DATA_LEN,               \
                 "Payload of " STRINGIFY(event_type) " too large for relay event");                \
    __ZMK_RELAY_ASSERT_NAME(identifier)

struct zmk_relay_event_received {
    uint8_t source;
    const char *event_name;
    const uint8_t *event_data;
    size_t event_data_size;
};

#include <zmk/split/transport/types.h>

ZMK_EVENT_DECLARE(zmk_relay_event_received);

/**
 * @brief Define event listener to handle relayed event for event_type.
 *        event_type is raised when a relay event with matching identifier is received.
 * @param event_type name of event struct type
 * @param identifier short unique identifier for this event type to distinguish type in relay event
 * @param source_field_name optional name of the field in event_type struct that indicates source.
 *                          If not specified, only directional relay is supported and bidirectional
 *                          relay causes infinite loops.
 */
#define ZMK_RELAY_EVENT_HANDLE(event_type, identifier, source_field_name)                          \
    __ZMK_RELAY_ASSERT_SIZE(event_type, identifier)                                                \
                                                                                                   \
    static char *event_type##_relay_id = STRINGIFY(identifier);                                    \
    static int zmk_split_relay_event_listener_##event_type##_cb(const zmk_event_t *eh) {           \
        struct zmk_relay_event_received *ev = as_zmk_relay_event_received(eh);                     \
        if (ev && strcmp(event_type##_relay_id, ev->event_name) == 0) {                            \
            struct event_type original_ev;                                                         \
            if (sizeof(struct event_type) == ev->event_data_size) {                                \
                memcpy(&original_ev, ev->event_data, sizeof(struct event_type));                   \
            } else {                                                                               \
                LOG_WRN("Relay event data size mismatch for event type %s: expected %d, got %d",   \
                        event_type##_relay_id, sizeof(struct event_type), ev->event_data_size);    \
                return ZMK_EV_EVENT_BUBBLE;                                                        \
            }                                                                                      \
            COND_CODE_1(IS_EMPTY(source_field_name), (),                                           \
                        (original_ev.source_field_name = ev->source + 1; /* 0 is central */))      \
                                                                                                   \
            raise_##event_type(original_ev);                                                       \
            return ZMK_EV_EVENT_HANDLED;                                                           \
        }                                                                                          \
        return ZMK_EV_EVENT_BUBBLE;                                                                \
    }                                                                                              \
    ZMK_LISTENER(event_type##_relay_handle, zmk_split_relay_event_listener_##event_type##_cb);     \
    ZMK_SUBSCRIPTION(event_type##_relay_handle, zmk_relay_event_received);

/**
 * @brief Like ZMK_RELAY_EVENT_HANDLE, but reconstructs event_type from the wire
 *        bytes with a caller-provided deserialize function instead of a raw
 *        memcpy. Pair this with the *_SERIALIZE sender macros when the event
 *        struct is not safe/compact to relay verbatim (embedded pointers,
 *        variable-length or endian-sensitive payloads, ...).
 * @param event_type name of event struct type
 * @param identifier short unique identifier for this event type to distinguish type in relay event
 * @param source_field_name optional name of the field in event_type struct that indicates source.
 *                          If not specified, only directional relay is supported and bidirectional
 *                          relay causes infinite loops.
 * @param deserialize_fn function with signature
 *                       `int fn(struct event_type *ev, const uint8_t *event_data,
 *                               size_t event_data_size)` that fills `ev` from the
 *                       received bytes and returns 0 on success or a negative
 *                       errno to drop the event.
 */
#define ZMK_RELAY_EVENT_HANDLE_DESERIALIZE(event_type, identifier, source_field_name,              \
                                           deserialize_fn)                                         \
    __ZMK_RELAY_ASSERT_NAME(identifier)                                                            \
                                                                                                   \
    static char *event_type##_relay_id = STRINGIFY(identifier);                                    \
    static int zmk_split_relay_event_listener_##event_type##_cb(const zmk_event_t *eh) {           \
        struct zmk_relay_event_received *ev = as_zmk_relay_event_received(eh);                     \
        if (ev && strcmp(event_type##_relay_id, ev->event_name) == 0) {                            \
            struct event_type original_ev = {0};                                                   \
            int __ret = deserialize_fn(&original_ev, ev->event_data, ev->event_data_size);         \
            if (__ret < 0) {                                                                       \
                LOG_WRN("Relay event deserialize failed for event type %s: %d",                    \
                        event_type##_relay_id, __ret);                                             \
                return ZMK_EV_EVENT_BUBBLE;                                                        \
            }                                                                                      \
            COND_CODE_1(IS_EMPTY(source_field_name), (),                                           \
                        (original_ev.source_field_name = ev->source + 1; /* 0 is central */))      \
                                                                                                   \
            raise_##event_type(original_ev);                                                       \
            return ZMK_EV_EVENT_HANDLED;                                                           \
        }                                                                                          \
        return ZMK_EV_EVENT_BUBBLE;                                                                \
    }                                                                                              \
    ZMK_LISTENER(event_type##_relay_handle, zmk_split_relay_event_listener_##event_type##_cb);     \
    ZMK_SUBSCRIPTION(event_type##_relay_handle, zmk_relay_event_received);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

/**
 * @brief Define event listener for _event_type to send given event to peripheral.
 * @param event_type name of event struct type
 * @param identifier short unique identifier for this event type to distinguish type in relay event
 * @param source_field_name optional name of the field in event_type struct that indicates source.
 *                          If not specified, only directional relay is supported and bidirectional
 *                          relay causes infinite loops.
 */
#define ZMK_RELAY_EVENT_CENTRAL_TO_PERIPHERAL(_event_type, identifier, source_field_name)          \
    __ZMK_RELAY_ASSERT_SIZE(_event_type, identifier)                                               \
    static char *_event_type##_central_relay_id = STRINGIFY(identifier);                           \
    static int zmk_split_central_relay_event_listener_##_event_type##_cb(const zmk_event_t *eh) {  \
        struct _event_type *ev = as_##_event_type(eh);                                             \
        if (ev && COND_CODE_1(IS_EMPTY(source_field_name), (true),                                 \
                              (ev->source_field_name == ZMK_RELAY_EVENT_SOURCE_SELF))) {           \
            struct zmk_split_relay_event_payload payload;                                          \
            payload.header.event_data_size = sizeof(struct _event_type);                           \
            strcpy(payload.event_type, _event_type##_central_relay_id);                            \
            memcpy(payload.event_data, ev, sizeof(struct _event_type));                            \
            payload.header.event_type_size = strlen(_event_type##_central_relay_id);               \
            zmk_split_central_send_relay_event(&payload);                                          \
        }                                                                                          \
        return ZMK_EV_EVENT_BUBBLE;                                                                \
    }                                                                                              \
    ZMK_LISTENER(_event_type##_relay, zmk_split_central_relay_event_listener_##_event_type##_cb);  \
    ZMK_SUBSCRIPTION(_event_type##_relay, _event_type);

/**
 * @brief Like ZMK_RELAY_EVENT_CENTRAL_TO_PERIPHERAL, but produces the wire bytes
 *        with a caller-provided serialize function instead of a raw memcpy of
 *        the whole struct. Pair with ZMK_RELAY_EVENT_HANDLE_DESERIALIZE.
 * @param _event_type name of event struct type
 * @param identifier short unique identifier for this event type to distinguish type in relay event
 * @param source_field_name optional name of the field in event_type struct that indicates source.
 *                          If not specified, only directional relay is supported and bidirectional
 *                          relay causes infinite loops.
 * @param serialize_fn function with signature
 *                     `int fn(const struct _event_type *ev, uint8_t *event_data,
 *                             size_t max_size)` that writes the event into
 *                     `event_data` (at most `max_size` bytes) and returns the
 *                     number of bytes written, or a negative errno to skip relay.
 */
#define ZMK_RELAY_EVENT_CENTRAL_TO_PERIPHERAL_SERIALIZE(_event_type, identifier,                   \
                                                        source_field_name, serialize_fn)           \
    __ZMK_RELAY_ASSERT_NAME(identifier)                                                            \
    static char *_event_type##_central_relay_id = STRINGIFY(identifier);                           \
    static int zmk_split_central_relay_event_listener_##_event_type##_cb(const zmk_event_t *eh) {  \
        struct _event_type *ev = as_##_event_type(eh);                                             \
        if (ev && COND_CODE_1(IS_EMPTY(source_field_name), (true),                                 \
                              (ev->source_field_name == ZMK_RELAY_EVENT_SOURCE_SELF))) {           \
            struct zmk_split_relay_event_payload payload;                                          \
            int __size =                                                                           \
                serialize_fn(ev, payload.event_data, CONFIG_ZMK_SPLIT_RELAY_EVENT_DATA_LEN);       \
            if (__size < 0 || __size > CONFIG_ZMK_SPLIT_RELAY_EVENT_DATA_LEN) {                    \
                LOG_WRN("Relay event serialize failed for event type %s: %d",                      \
                        _event_type##_central_relay_id, __size);                                   \
                return ZMK_EV_EVENT_BUBBLE;                                                        \
            }                                                                                      \
            payload.header.event_data_size = __size;                                               \
            strcpy(payload.event_type, _event_type##_central_relay_id);                            \
            payload.header.event_type_size = strlen(_event_type##_central_relay_id);               \
            zmk_split_central_send_relay_event(&payload);                                          \
        }                                                                                          \
        return ZMK_EV_EVENT_BUBBLE;                                                                \
    }                                                                                              \
    ZMK_LISTENER(_event_type##_relay, zmk_split_central_relay_event_listener_##_event_type##_cb);  \
    ZMK_SUBSCRIPTION(_event_type##_relay, _event_type);

#define ZMK_RELAY_EVENT_PERIPHERAL_TO_CENTRAL(_event_type, identifier, source_field_name)

#define ZMK_RELAY_EVENT_PERIPHERAL_TO_CENTRAL_SERIALIZE(_event_type, identifier,                   \
                                                        source_field_name, serialize_fn)

#else // peripheral

#include <zmk/split/peripheral.h>

#define ZMK_RELAY_EVENT_CENTRAL_TO_PERIPHERAL(_event_type, identifier, source_field_name)

#define ZMK_RELAY_EVENT_CENTRAL_TO_PERIPHERAL_SERIALIZE(_event_type, identifier,                   \
                                                        source_field_name, serialize_fn)

/**
 * @brief Define event listener for _event_type to send given event to central.
 * @param _event_type name of event struct type
 * @param identifier short unique identifier for this event type to distinguish type in relay event
 * @param source_field_name optional name of the field in event_type struct that indicates source.
 *                          If not specified, only directional relay is supported and bidirectional
 *                          relay causes infinite loops.
 */
#define ZMK_RELAY_EVENT_PERIPHERAL_TO_CENTRAL(_event_type, identifier, source_field_name)          \
    __ZMK_RELAY_ASSERT_SIZE(_event_type, identifier)                                               \
                                                                                                   \
    static char *_event_type##_peripheral_relay_id = STRINGIFY(identifier);                        \
                                                                                                   \
    static int zmk_split_peripheral_relay_event_listener_##_event_type##_cb(                       \
        const zmk_event_t *eh) {                                                                   \
        struct _event_type *ev = as_##_event_type(eh);                                             \
        if (ev && COND_CODE_1(IS_EMPTY(source_field_name), (true),                                 \
                              (ev->source_field_name == ZMK_RELAY_EVENT_SOURCE_SELF))) {           \
            struct zmk_split_transport_peripheral_event pev = {                                    \
                .type = ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_RELAY_EVENT,                     \
                .data = {.relay_event = {                                                          \
                             .header = {.event_data_size = sizeof(struct _event_type)},            \
                         }}};                                                                      \
            strcpy(pev.data.relay_event.event_type, _event_type##_peripheral_relay_id);            \
            memcpy(pev.data.relay_event.event_data, ev, sizeof(struct _event_type));               \
            return zmk_split_peripheral_report_event(&pev);                                        \
        }                                                                                          \
        return ZMK_EV_EVENT_BUBBLE;                                                                \
    }                                                                                              \
    ZMK_LISTENER(_event_type##_relay,                                                              \
                 zmk_split_peripheral_relay_event_listener_##_event_type##_cb);                    \
    ZMK_SUBSCRIPTION(_event_type##_relay, _event_type);

/**
 * @brief Like ZMK_RELAY_EVENT_PERIPHERAL_TO_CENTRAL, but produces the wire bytes
 *        with a caller-provided serialize function instead of a raw memcpy of
 *        the whole struct. Pair with ZMK_RELAY_EVENT_HANDLE_DESERIALIZE.
 * @param _event_type name of event struct type
 * @param identifier short unique identifier for this event type to distinguish type in relay event
 * @param source_field_name optional name of the field in event_type struct that indicates source.
 *                          If not specified, only directional relay is supported and bidirectional
 *                          relay causes infinite loops.
 * @param serialize_fn function with signature
 *                     `int fn(const struct _event_type *ev, uint8_t *event_data,
 *                             size_t max_size)` that writes the event into
 *                     `event_data` (at most `max_size` bytes) and returns the
 *                     number of bytes written, or a negative errno to skip relay.
 */
#define ZMK_RELAY_EVENT_PERIPHERAL_TO_CENTRAL_SERIALIZE(_event_type, identifier,                   \
                                                        source_field_name, serialize_fn)           \
    __ZMK_RELAY_ASSERT_NAME(identifier)                                                            \
                                                                                                   \
    static char *_event_type##_peripheral_relay_id = STRINGIFY(identifier);                        \
                                                                                                   \
    static int zmk_split_peripheral_relay_event_listener_##_event_type##_cb(                       \
        const zmk_event_t *eh) {                                                                   \
        struct _event_type *ev = as_##_event_type(eh);                                             \
        if (ev && COND_CODE_1(IS_EMPTY(source_field_name), (true),                                 \
                              (ev->source_field_name == ZMK_RELAY_EVENT_SOURCE_SELF))) {           \
            struct zmk_split_transport_peripheral_event pev = {                                    \
                .type = ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_RELAY_EVENT,                     \
            };                                                                                     \
            int __size = serialize_fn(ev, pev.data.relay_event.event_data,                         \
                                      CONFIG_ZMK_SPLIT_RELAY_EVENT_DATA_LEN);                      \
            if (__size < 0 || __size > CONFIG_ZMK_SPLIT_RELAY_EVENT_DATA_LEN) {                    \
                LOG_WRN("Relay event serialize failed for event type %s: %d",                      \
                        _event_type##_peripheral_relay_id, __size);                                \
                return ZMK_EV_EVENT_BUBBLE;                                                        \
            }                                                                                      \
            pev.data.relay_event.header.event_data_size = __size;                                  \
            strcpy(pev.data.relay_event.event_type, _event_type##_peripheral_relay_id);            \
            return zmk_split_peripheral_report_event(&pev);                                        \
        }                                                                                          \
        return ZMK_EV_EVENT_BUBBLE;                                                                \
    }                                                                                              \
    ZMK_LISTENER(_event_type##_relay,                                                              \
                 zmk_split_peripheral_relay_event_listener_##_event_type##_cb);                    \
    ZMK_SUBSCRIPTION(_event_type##_relay, _event_type);

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)