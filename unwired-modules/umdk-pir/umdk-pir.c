/*
 * Copyright (C) 2016-2018 Unwired Devices LLC <info@unwds.com>

 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software
 * is furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @defgroup    
 * @ingroup     
 * @brief       
 * @{
 * @file		umdk-pir.c
 * @brief       umdk-pir module implementation
 * @author      MC
 * @author		Evgeniy Ponomarev
 */

#ifdef __cplusplus
extern "C" {
#endif

/* define is autogenerated, do not change */
#undef _UMDK_MID_
#define _UMDK_MID_ UNWDS_PIR_MODULE_ID

/* define is autogenerated, do not change */
#undef _UMDK_NAME_
#define _UMDK_NAME_ "pir"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "periph/gpio.h"

#include "board.h"

#include "umdk-ids.h"
#include "unwds-common.h"
#include "umdk-pir.h"

#include "thread.h"
#include "xtimer.h"

static kernel_pid_t handler_pid;

static msg_t pir;

static int last_pressed[4] = { 0, };

static uwnds_cb_t *callback;

static void *handler(void *arg) {
    (void)arg;
    
    msg_t msg;
    msg_t msg_queue[4];
    msg_init_queue(msg_queue, 4);

    while (1) {
        msg_receive(&msg);
        int val = msg.content.value;

        module_data_t data;
        data.length = 2;
        data.data[0] = _UMDK_MID_;
        data.data[1] = val;

        callback(&data);
    }

	return NULL;
}

static void pir_int_cb(void *arg) {
	(void) arg;

    int now = xtimer_now().ticks32;
    /* Don't accept a press of current button if it did occur earlier than last press plus debouncing time */
    if (now - last_pressed[0] <= UMDK_PIR_DEBOUNCE_TIME_MS * 1000) {
    	last_pressed[0] = now;
    	return;
	}
    last_pressed[0] = now;
    
    /* Prepare event messages */
    pir.content.value = gpio_read(UMDK_PIR);

	msg_send_int(&pir, handler_pid);
}

void umdk_pir_init(uwnds_cb_t *event_callback) {

	callback = event_callback;

	/* Initialize interrupts */
	gpio_init_int(UMDK_PIR, GPIO_IN_PD, GPIO_BOTH, pir_int_cb, NULL);

	/* Create handler thread */
	char *stack = (char *) allocate_stack(UMDK_PIR_STACK_SIZE);
	if (!stack) {
		return;
	}

	handler_pid = thread_create(stack, UMDK_PIR_STACK_SIZE, THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST, handler, NULL, "PIR thread");
}

bool umdk_pir_cmd(module_data_t *data, module_data_t *reply) {
    (void)data;
    (void)reply;
    
	return false;
}

#ifdef __cplusplus
}
#endif
