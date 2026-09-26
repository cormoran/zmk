/*
 * Copyright (c) 2022 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <stdint.h>
#include <zmk/behavior.h>

/**
 * @brief Queue a binding followed by a wait in milliseconds.
 *
 * @param deadline Absolute uptime in milliseconds (low 32 bits), or zero for
 *                 no deadline. Nonzero deadlines must be less than INT32_MAX
 *                 milliseconds in the future. The wait is shortened to fit the
 *                 deadline, including waits already queued and the active wait.
 *                 Bindings that cannot start before the deadline are discarded.
 *
 * The deadline is checked again before invocation and after the callback, so
 * callback/scheduler delays do not add a wait past the deadline. A callback
 * itself cannot be preempted by this API. A zero wait remains valid when the
 * binding can start before its deadline.
 *
 * @retval 0 Queued or discarded due to the deadline.
 * @retval Negative errno code if the queue is full.
 */
int zmk_behavior_queue_add(const struct zmk_behavior_binding_event *event,
                           const struct zmk_behavior_binding behavior, bool press, uint32_t wait,
                           uint32_t deadline);
