/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior_queue.h>

/* Use real Zephyr message queues and locks, with a deterministic clock and
 * manually driven worker. This exercises the production queue implementation
 * without relying on host scheduler timing for deadline assertions.
 */
static int64_t now;
static int64_t scheduled_wait;
static int64_t callback_duration;
static bool enqueue_in_callback;
static unsigned int calls;
static uint32_t positions[16];
static bool presses[16];
static int64_t timestamps[16];
static struct zmk_behavior_binding binding = {.behavior_dev = "test"};
static struct zmk_behavior_binding_event event = {
    .position = 1,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    .source = 3,
#endif
};

static int64_t test_uptime_get(void) { return now; }

static int test_work_schedule(struct k_work_delayable *work, k_timeout_t delay) {
    scheduled_wait = delay.ticks < K_TICKS_FOREVER
                         ? k_ticks_to_ms_floor64(Z_TICK_ABS(delay.ticks)) - now
                         : k_ticks_to_ms_floor64(delay.ticks);
    return 1;
}

#define k_uptime_get test_uptime_get
#define k_work_schedule test_work_schedule
#include "../../../src/behavior_queue.c"
#undef k_work_schedule
#undef k_uptime_get

int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *src_binding,
                                struct zmk_behavior_binding_event evt, bool pressed) {
    zassert_true(calls < ARRAY_SIZE(positions));
    positions[calls] = evt.position;
    presses[calls] = pressed;
    timestamps[calls++] = evt.timestamp;
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    zassert_equal(evt.source, event.source);
#endif
    now += callback_duration;
    if (enqueue_in_callback) {
        enqueue_in_callback = false;
        struct zmk_behavior_binding_event nested = event;
        nested.position = 2;
        /* The callback's own 100ms wait leaves only 20ms for this binding. */
        zassert_ok(zmk_behavior_queue_add_with_deadline(&nested, binding, false, 50, now + 120));
        zassert_equal(calls, 1, "Nested enqueue must not invoke recursively");
    }
    return 0;
}

static void before(void *fixture) {
    k_msgq_purge(&zmk_behavior_queue_msgq);
    queued_wait = 0;
    invoking_wait = 0;
    wait_until = 0;
    running = false;
    now = 1000;
    scheduled_wait = 0;
    callback_duration = 0;
    enqueue_in_callback = false;
    calls = 0;
}

static int add(uint32_t wait, uint32_t deadline) {
    return zmk_behavior_queue_add_with_deadline(&event, binding, true, wait, deadline);
}

static void resume(int64_t at) {
    now = at;
    scheduled_wait = 0;
    behavior_queue_process_next(&queue_work.work);
}

ZTEST(behavior_queue, test_no_deadline_fifo) {
    zassert_ok(zmk_behavior_queue_add(&event, binding, true, 100));
    event.position = 2;
    zassert_ok(zmk_behavior_queue_add(&event, binding, false, 0));
    event.position = 1;
    zassert_equal(calls, 1);
    resume(1100);
    zassert_equal(calls, 2);
    zassert_equal(positions[0], 1);
    zassert_equal(positions[1], 2);
    zassert_true(presses[0]);
    zassert_false(presses[1]);
    zassert_equal(timestamps[1], 1100);
    zassert_false(running);
}

ZTEST(behavior_queue, test_enqueue_clips_total_wait) {
    zassert_ok(add(100, 0));
    zassert_ok(add(30, 0));
    zassert_ok(add(100, 1150));
    zassert_equal(queued_wait, 50);
    resume(1100);
    zassert_equal(scheduled_wait, 30);
    resume(1130);
    zassert_equal(scheduled_wait, 20);
    zassert_equal(queued_wait, 0);
}

ZTEST(behavior_queue, test_elapsed_active_wait) {
    zassert_ok(add(100, 0));
    now = 1080;
    zassert_ok(add(100, 1150));
    zassert_equal(queued_wait, 50);
}

ZTEST(behavior_queue, test_no_room_skips_before_push) {
    zassert_ok(add(100, 0));
    zassert_ok(add(20, 1100));
    zassert_ok(add(0, 1100));
    zassert_ok(add(20, 1099));
    zassert_equal(k_msgq_num_used_get(&zmk_behavior_queue_msgq), 0);
    zassert_equal(queued_wait, 0);
}

ZTEST(behavior_queue, test_expired_deadline) {
    zassert_ok(add(10, 999));
    zassert_ok(add(10, 1000));
    zassert_equal(calls, 0);
    zassert_false(running);
}

ZTEST(behavior_queue, test_zero_wait_before_deadline) {
    zassert_ok(add(0, 1001));
    zassert_equal(calls, 1);
    zassert_equal(scheduled_wait, 0);
    zassert_false(running);
}

ZTEST(behavior_queue, test_expired_on_dequeue_releases_accounting) {
    zassert_ok(add(100, 0));
    zassert_ok(add(50, 1200));
    zassert_ok(add(10, 0));
    resume(1200);
    zassert_equal(calls, 2);
    zassert_equal(scheduled_wait, 10);
    zassert_equal(queued_wait, 0);
    zassert_ok(add(20, 1230));
    zassert_equal(queued_wait, 20);
}

ZTEST(behavior_queue, test_late_worker_clips_wait) {
    zassert_ok(add(100, 0));
    zassert_ok(add(100, 1200));
    resume(1150);
    zassert_equal(calls, 2);
    zassert_equal(scheduled_wait, 50);
}

ZTEST(behavior_queue, test_callback_duration_clips_wait) {
    callback_duration = 40;
    zassert_ok(add(100, 1100));
    zassert_equal(scheduled_wait, 60);
}

ZTEST(behavior_queue, test_callback_overrun_has_no_wait) {
    callback_duration = 101;
    zassert_ok(add(100, 1100));
    zassert_equal(calls, 1);
    zassert_equal(scheduled_wait, 0);
    zassert_false(running);
}

ZTEST(behavior_queue, test_no_deadline_keeps_wait_after_callback) {
    callback_duration = 40;
    zassert_ok(add(100, 0));
    zassert_equal(scheduled_wait, 100);
}

ZTEST(behavior_queue, test_reentrant_enqueue_accounts_for_invoking_wait) {
    enqueue_in_callback = true;
    zassert_ok(add(100, 0));
    zassert_equal(queued_wait, 20);
    resume(1100);
    zassert_equal(calls, 2);
    zassert_equal(positions[1], 2);
    zassert_equal(scheduled_wait, 20);
}

ZTEST(behavior_queue, test_full_queue_does_not_change_accounting) {
    zassert_ok(add(100, 0));
    for (int i = 0; i < CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE; i++) {
        zassert_ok(add(10, 0));
    }
    zassert_true(add(10, 0) < 0);
    zassert_equal(queued_wait, 40);
    /* Deadline discards still succeed even when the queue is full. */
    zassert_ok(add(10, 1100));
    zassert_equal(queued_wait, 40);
}

ZTEST(behavior_queue, test_timestamp_wraparound) {
    now = UINT32_MAX - 10LL;
    zassert_ok(add(100, 20));
    zassert_true(scheduled_wait > 0);
    zassert_true(scheduled_wait <= 31, "Tick rounding must not extend the deadline");
    zassert_ok(add(0, UINT32_MAX - 11));
    zassert_equal(k_msgq_num_used_get(&zmk_behavior_queue_msgq), 0);
}

ZTEST(behavior_queue, test_deadline_timeout_rounds_down) {
    zassert_ok(add(31, 1031));
    zassert_true(scheduled_wait > 0);
    zassert_true(scheduled_wait <= 31);
}

ZTEST(behavior_queue, test_total_wait_does_not_overflow_uint32) {
    zassert_ok(add(INT32_MAX, 0));
    zassert_ok(add(INT32_MAX, 0));
    zassert_ok(add(INT32_MAX, 0));
    zassert_equal(queued_wait, 2ULL * INT32_MAX);
    zassert_ok(add(100, now + 1000));
    zassert_equal(k_msgq_num_used_get(&zmk_behavior_queue_msgq), 2);
}

ZTEST_SUITE(behavior_queue, NULL, NULL, before, NULL, NULL);
