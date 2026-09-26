/*
 * Copyright (c) 2022 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zmk/behavior_queue.h>
#include <zmk/behavior.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct q_item {
    uint32_t position;
    uint32_t deadline;
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    uint8_t source;
#endif
    struct zmk_behavior_binding binding;
    bool press : 1;
    uint32_t wait : 31;
};

K_MSGQ_DEFINE(zmk_behavior_queue_msgq, sizeof(struct q_item), CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE, 4);

static void behavior_queue_process_next(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(queue_work, behavior_queue_process_next);

/* Protect the queue and its wait accounting together. Never hold this lock while
 * invoking a behavior: macros can enqueue more bindings from their callbacks.
 */
static struct k_spinlock queue_lock;
static uint64_t queued_wait;
static uint32_t invoking_wait;
static int64_t wait_until;
static bool running;

/* Like uptime32, deadlines wrap. Callers must use deadlines less than INT32_MAX
 * milliseconds away so that past and future timestamps are unambiguous.
 */
static int32_t deadline_remaining(uint32_t deadline, int64_t now) {
    return (int32_t)(deadline - (uint32_t)now);
}

static void behavior_queue_process_next(struct k_work *work) {
    struct q_item item;
    k_spinlock_key_t key = k_spin_lock(&queue_lock);
    wait_until = 0;

    while (k_msgq_get(&zmk_behavior_queue_msgq, &item, K_NO_WAIT) == 0) {
        queued_wait -= item.wait;
        int64_t now = k_uptime_get();
        if (item.deadline != 0) {
            int32_t remaining = deadline_remaining(item.deadline, now);
            if (remaining <= 0) {
                continue;
            }
            item.wait = MIN(item.wait, (uint32_t)remaining);
        }
        invoking_wait = item.wait;
        k_spin_unlock(&queue_lock, key);

        LOG_DBG("Invoking %s: 0x%02x 0x%02x", item.binding.behavior_dev, item.binding.param1,
                item.binding.param2);

        struct zmk_behavior_binding_event event = {.position = item.position,
                                                   .timestamp = now,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
                                                   .source = item.source
#endif
        };
        zmk_behavior_invoke_binding(&item.binding, event, item.press);

        key = k_spin_lock(&queue_lock);
        invoking_wait = 0;
        now = k_uptime_get();
        if (item.deadline != 0) {
            int32_t remaining = deadline_remaining(item.deadline, now);
            item.wait = remaining > 0 ? MIN(item.wait, (uint32_t)remaining) : 0;
        }

        LOG_DBG("Processing next queued behavior in %dms", item.wait);
        if (item.wait > 0) {
            wait_until = now + item.wait;
            /* An absolute timeout rounded down cannot intentionally extend a
             * deadline wait by rounding up or by the relative timeout's extra
             * tick. Scheduler latency is handled by the next deadline check.
             */
            k_timeout_t timeout = item.deadline != 0
                                      ? K_TIMEOUT_ABS_TICKS(k_ms_to_ticks_floor64(wait_until))
                                      : K_MSEC(item.wait);
            k_work_schedule(&queue_work, timeout);
            k_spin_unlock(&queue_lock, key);
            return;
        }
    }

    running = false;
    k_spin_unlock(&queue_lock, key);
}

int zmk_behavior_queue_add(const struct zmk_behavior_binding_event *event,
                           const struct zmk_behavior_binding binding, bool press, uint32_t wait,
                           uint32_t deadline) {
    struct q_item item = {
        .press = press,
        .binding = binding,
        .wait = wait,
        .deadline = deadline,
        .position = event->position,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = event->source,
#endif
    };

    k_spinlock_key_t key = k_spin_lock(&queue_lock);
    if (deadline != 0) {
        int64_t now = k_uptime_get();
        int32_t remaining = deadline_remaining(deadline, now);
        uint64_t total_wait = queued_wait + invoking_wait;
        if (wait_until > now) {
            total_wait += wait_until - now;
        }
        if (remaining <= 0 || total_wait >= (uint32_t)remaining) {
            k_spin_unlock(&queue_lock, key);
            return 0;
        }
        item.wait = MIN(item.wait, (uint32_t)remaining - total_wait);
    }

    const int ret = k_msgq_put(&zmk_behavior_queue_msgq, &item, K_NO_WAIT);
    if (ret < 0) {
        k_spin_unlock(&queue_lock, key);
        return ret;
    }
    queued_wait += item.wait;

    bool start = !running;
    running = true;
    k_spin_unlock(&queue_lock, key);
    if (start) {
        behavior_queue_process_next(&queue_work.work);
    }

    return 0;
}
