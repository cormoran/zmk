/*
 * Copyright (c) 2021 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <zephyr/drivers/sensor.h>

#include "battery_common.h"

int battery_channel_get(const struct battery_value *value, enum sensor_channel chan,
                        struct sensor_value *val_out) {
    switch (chan) {
    case SENSOR_CHAN_GAUGE_VOLTAGE:
        val_out->val1 = value->millivolts / 1000;
        val_out->val2 = (value->millivolts % 1000) * 1000U;
        break;

    case SENSOR_CHAN_GAUGE_STATE_OF_CHARGE:
        val_out->val1 = value->state_of_charge;
        val_out->val2 = 0;
        break;

    default:
        return -ENOTSUP;
    }

    return 0;
}

uint8_t lithium_ion_mv_to_pct(int16_t bat_mv) {
    // Simple linear approximation of a battery based off adafruit's discharge graph:
    // https://learn.adafruit.com/li-ion-and-lipoly-batteries/voltages

    if (bat_mv >= 4200) {
        return 100;
    } else if (bat_mv <= 3450) {
        return 0;
    }

    return bat_mv * 2 / 15 - 459;
}

uint8_t mv_to_pct_linear_interpolation(int16_t bat_mv, int16_t *mv_thresholds,
                                       size_t mv_thresholds_size) {
    if (bat_mv < mv_thresholds[0]) {
        return 0;
    }
    if (bat_mv >= mv_thresholds[mv_thresholds_size - 1]) {

        return 100;
    }
    for (size_t i = 1; i < mv_thresholds_size; i++) {
        if (bat_mv < mv_thresholds[i]) {
            int low = mv_thresholds[i - 1];
            int high = mv_thresholds[i];
            // [i-1] -> bat_mv -> [i]
            //  A: [0  ]->[i-1 ] = i-1 units = (i-1) / (size - 1)
            //  B: [i-1]->bat_mv = 1 unit * (bat_mv - low) / (high - low)
            //                   = 1 / (size - 1) * (bat_mv - low) / (high - low)
            // percentage = (A + B) * 100
            float deno = ((bat_mv - low) + (i - 1) * (high - low)) * 100;
            return deno / (float)((high - low) * (mv_thresholds_size - 1));
        }
    }
    return 100;
}
