/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <proto/zmk/custom.pb.h>
#include <zmk/event_manager.h>
#include <zmk/studio/rpc.h>

struct zmk_studio_custom_notification {
    uint8_t subsystem_index;
    // callback to encode payload
    // Note that as for now, encode is executed within raise_zmk_studio_custom_notification(...)
    // caller can safely set pointer to local stack variable which remains until
    // raise_zmk_studio_custom_notification(...) returns.
    pb_callback_t encode_payload;
};

ZMK_EVENT_DECLARE(zmk_studio_custom_notification);

/**
 * Handler function which is called when a custom subsystem CallRequest is received.
 * @param request The CallRequest received from the studio.
 * @param encode_response pointer to CallResponse.payload's pb_callback_t.
 * The handler should set this to a function which encodes the response.
 *
 * WARNING: The encoding can be called multiple times to calculate the size
 * of the payload.
 *
 * WARNING: The encoding is executed after returning from this handler, so the pb_callback_t
 * **cannot use pointer to local stack variables**. Pointer to data segment (global/static variable)
 * or heap memory should be used. Instead of setting data pointer, the handler can also collect and
 * build response in encoder function if the operation is light-weight and does not have side
 * effects.
 *
 * NOTE: As for now, the handler is not called in parallel. Next RPC is always processed after
 * sending response. So the handler can share single global variable as data buffer to use in
 * encoder function.
 *
 * @return true if the request was handled successfully, false if failed.
 */
typedef bool(custom_subsystem_handler)(const zmk_custom_CallRequest *request,
                                       pb_callback_t *encode_response);

struct zmk_rpc_custom_subsystem_meta {
    char **ui_urls;
    size_t ui_urls_count;
    enum zmk_studio_rpc_handler_security security;
};

struct zmk_rpc_custom_subsystem {
    char *identifier;
    struct zmk_rpc_custom_subsystem_meta *meta;
    custom_subsystem_handler *handler;
};

#define ZMK_RPC_CUSTOM_SUBSYSTEM(_identifier, _meta, _handler)                                     \
    BUILD_ASSERT(sizeof(#_identifier) - 1 <=                                                       \
                     CONFIG_ZMK_STUDIO_RPC_CUSTOM_SUBSYSTEM_IDENTIFIER_MAX_LEN,                    \
                 "Identifier too long: " #_identifier);                                            \
    STRUCT_SECTION_ITERABLE(zmk_rpc_custom_subsystem, zmk_rpc_custom_subsystem_##_identifier) = {  \
        .identifier = #_identifier,                                                                \
        .meta = _meta,                                                                             \
        .handler = _handler,                                                                       \
    };
