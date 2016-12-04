/*
 * Copyright (C) 2016 Unwired Devices [info@unwds.com]
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup
 * @ingroup
 * @brief
 * @{
 * @file		umdk-lps331.c
 * @brief       umdk-lps331 module implementation
 * @author      Mikhail Churikov
 * @author		Evgeniy Ponomarev 
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "periph/gpio.h"
#include "periph/i2c.h"

#include "board.h"

#include "lps331ap.h"

#include "unwds-common.h"
#include "umdk-lps331.h"
#include "unwds-gpio.h"

#include "thread.h"
#include "xtimer.h"
#include "rtc-timers.h"

static lps331ap_t dev;

static uwnds_cb_t *callback;

static kernel_pid_t timer_pid;

static msg_t timer_msg = {
};
static rtctimer_t timer;

static struct {
	uint8_t is_valid;
	uint8_t publish_period_min;
	uint8_t i2c_dev;
} lps331_config;

static bool init_sensor(void)
{
    dev.i2c = UMDK_LPS331_I2C;

    return lps331ap_init(&dev, dev.i2c, 0x5D, LPS331AP_RATE_1HZ);
}

static void prepare_result(module_data_t *buf)
{
    int temperature_mc, pressure_mbar;
	uint16_t temperature_unwds, pressure_unwds;

    temperature_mc = lps331ap_read_temp(&dev);
	temperature_unwds = (temperature_mc/1000.0f + 100.0f) * 16.0f;
	
    pressure_mbar = lps331ap_read_pres(&dev);
	pressure_unwds = pressure_mbar >> 4;

	printf("[lps331] T: %d C, P: %d mbar\n", (temperature_unwds >> 4)-100, pressure_unwds << 4);

    buf->length = 1 + 2 + 2; /* One byte for module ID, two bytes for temperature, two bytes for pressure*/

    buf->data[0] = UNWDS_LPS331_MODULE_ID;

    /* Copy measurements into response */
    memcpy(buf->data + 1, &temperature_unwds, 2);
    memcpy(buf->data + 1 + 2, &pressure_unwds, 2);
}

static void *timer_thread(void *arg)
{
    msg_t msg;
    msg_t msg_queue[8];

    msg_init_queue(msg_queue, 8);

    puts("[umdk-lps331] Periodic publisher thread started");

    while (1) {
        msg_receive(&msg);

        rtctimers_remove(&timer);

        module_data_t data = {};
        prepare_result(&data);

        /* Notify the application */
        callback(&data);

        /* Restart after delay */
        rtctimers_set_msg(&timer, 60 * lps331_config.publish_period_min, &timer_msg, timer_pid);
    }

    return NULL;
}

static void reset_config(void) {
	lps331_config.is_valid = 0;
	lps331_config.publish_period_min = UMDK_LPS331_PUBLISH_PERIOD_MIN;
	lps331_config.i2c_dev = UMDK_LPS331_I2C;
}

static void init_config(void) {
	reset_config();

	if (!unwds_read_nvram_config(UNWDS_LPS331_MODULE_ID, (uint8_t *) &lps331_config, sizeof(lps331_config)))
		return;

	if ((lps331_config.is_valid == 0xFF) || (lps331_config.is_valid == 0))  {
		reset_config();
		return;
	}

	if (lps331_config.i2c_dev >= I2C_NUMOF) {
		reset_config();
		return;
	}
}

static inline void save_config(void) {
	lps331_config.is_valid = 1;
	unwds_write_nvram_config(UNWDS_LPS331_MODULE_ID, (uint8_t *) &lps331_config, sizeof(lps331_config));
}

void umdk_lps331_init(uint32_t *non_gpio_pin_map, uwnds_cb_t *event_callback)
{
    (void) non_gpio_pin_map;

    callback = event_callback;
	
	init_config();
	printf("[lps331] Publish period: %d min\n", lps331_config.publish_period_min);

    if (init_sensor()) {
        puts("[umdk-lps331] Unable to init sensor!");
    }
	
    if (lps331ap_enable(&dev) < 1) {
        puts("[umdk-lps331] Unable to enable sensor!");
    }

    /* Create handler thread */
    char *stack = (char *) allocate_stack();
    if (!stack) {
    	puts("umdk-lps331: unable to allocate memory. Are too many modules enabled?");
    	return;
    }

    timer_pid = thread_create(stack, UNWDS_STACK_SIZE_BYTES, THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST, timer_thread, NULL, "lps331ap thread");

    /* Start publishing timer */
    rtctimers_set_msg(&timer, 60 * lps331_config.publish_period_min, &timer_msg, timer_pid);
}

bool umdk_lps331_cmd(module_data_t *cmd, module_data_t *reply)
{
    if (cmd->length < 1) {
        return false;
    }

    umdk_lps331_cmd_t c = cmd->data[0];
    switch (c) {
        case UMDK_LPS331_CMD_SET_PERIOD: {
            if (cmd->length != 2) {
                return false;
            }

            uint8_t period = cmd->data[1];
            rtctimers_remove(&timer);

            lps331_config.publish_period_min = period;
			save_config();

            /* Don't restart timer if new period is zero */
            if (lps331_config.publish_period_min) {
            	rtctimers_set_msg(&timer, 60 * lps331_config.publish_period_min, &timer_msg, timer_pid);
                printf("[umdk-lps331] Period set to %d seconds\n", lps331_config.publish_period_min);
            }
            else {
                puts("[umdk-lps331] Timer stopped");
            }

            reply->length = 4;
            reply->data[0] = UNWDS_LPS331_MODULE_ID;
            reply->data[1] = 'o';
            reply->data[2] = 'k';
            reply->data[3] = '\0';

            break;
        }

        case UMDK_LPS331_CMD_POLL:
            /* Send signal to publisher thread */
            msg_send(&timer_msg, timer_pid);

            return false; /* Don't reply */

            break;

        case UMDK_LPS331_CMD_SET_I2C: {
            i2c_t i2c = (i2c_t) cmd->data[1];
            dev.i2c = i2c;
			lps331_config.i2c_dev = i2c;

            init_sensor();

            return false; /* Don't reply */

            break;
        }

        default:
            break;
    }

    return true;
}

#ifdef __cplusplus
}
#endif