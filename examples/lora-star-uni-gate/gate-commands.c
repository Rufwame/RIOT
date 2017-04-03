/*
 * Copyright (C) 2016 Unwired Devices
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
 * @file		gate-commands.c
 * @brief       gate mqtt-uart commands implementation
 * @author      Eugene Ponomarev
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "pending-fifo.h"
#include "gate-commands.h"
#include "ls-gate.h"
#include "unwds-common.h"

static void exec_command(ls_gate_t *ls, kernel_pid_t writer, gc_pending_fifo_t *fifo, char *data) {
	ls_gate_devices_t *devs = &ls->devices;
	ls_gate_channel_t *channels = ls->channels;

	gate_cmd_type_t c = data[0];
	char *payload = data + 1;

	switch (c) {
	case CMD_PING:
		/* Send pong response */
		gc_pending_fifo_push(fifo, "!\n");

		/* Send flush message */
		msg_t msg;
		msg_send(&msg, writer);

		break;

	case CMD_DEVLIST:
		for (int i = 0; i < LS_GATE_MAX_NODES; i++) {
			if (!devs->nodes_free_list[i]) {
				char buf[128];

				/* L */
				sprintf(buf, "%c%08X%08X%08X%08X%04X%04X\n", REPLY_LIST,
						(unsigned int) (devs->nodes[i].node_id >> 32), (unsigned int) (devs->nodes[i].node_id & 0xFFFFFFFF),
						(unsigned int) (devs->nodes[i].app_id >> 32), (unsigned int) (devs->nodes[i].app_id & 0xFFFFFFFF),
						(unsigned int) ((ls->_internal.ping_count - devs->nodes[i].last_seen) * LS_PING_TIMEOUT_S),
						(unsigned int) devs->nodes[i].node_class);

				if (!gc_pending_fifo_push(fifo, buf)) {
					puts("gc: pending fifo overflowed!");
				}
			}
		}

		break;

	case CMD_IND: {
		/* Minimum length is length of eui64 in hex + two hex digits of cmd + one character '\n' */
		if (strlen(payload) < 16 + 2 + 1) {
			printf("[error] Invalid command received: %s\n", data);
			return;
		}

		uint64_t nodeid = 0;
		if (!hex_to_bytesn(payload, 16, (uint8_t *) &nodeid, true)) {
			printf("[error] Invalid command received: %s\n", data);
			return;
		}

		ls_gate_node_t *node = ls_devlist_get_by_nodeid(devs, nodeid);
		if (node == NULL) {
			puts("[error] Node with specified node ID is not found.\n");
			return;
		}

		/* Skip nodeid */
		payload += 16;

		/* Number of argument bytes is length of hex digits train except '\n' character */
		int numdigits = strlen(payload) - 1;
		if (numdigits % 2 != 0) {
			printf("[error] Invalid command data: %s, length must be even.\n", data);
			return;
		}

		uint8_t a[UNWDS_MAX_DATA_LEN + 2];
		if (!hex_to_bytesn(payload, numdigits, a, false)) {
			printf("[error] Invalid command received: %s\n", data);
			return;
		}

		/* Send LoRa message */
		ls_gate_send_to(ls, node->addr, a, numdigits / 2);
		break;
	}

	case CMD_FLUSH: {
		/* Send flush message */
		msg_t msg;
		msg_send(&msg, writer);
		break;
	}

	case CMD_HAS_PENDING: {
		/* Minimum length is length of eui64 in hex + two hex digits + one character '\n' */
		if (strlen(payload) < 16 + 2 + 1) {
			printf("[error] Invalid command received: %s\n", data);
			return;
		}

		uint64_t nodeid = 0;
		if (!hex_to_bytesn(payload, 16, (uint8_t *) &nodeid, true)) {
			printf("[error] Invalid command received: %s\n", data);
			return;
		}

		ls_gate_node_t *node = ls_devlist_get_by_nodeid(devs, nodeid);
		if (node == NULL) {
			puts("[error] Node with specified node ID is not found.\n");
			return;
		}

		/* Skip nodeid */
		payload += 16;

		uint8_t num_pending = strtol(payload, NULL, 16);
		node->num_pending = num_pending;

		printf("[pending] setting node 0x%08X%08X has %u frames pending\n",
				(unsigned int) (node->node_id >> 32),
				(unsigned int) (node->node_id & 0xFFFFFFFF), num_pending);

		break;
	}

	case CMD_INVITE: {
		/* Minimum length is length of eui64 in hex +  one character '\n' */
		if (strlen(payload) < 16 + 1) {
			printf("[error] Invalid command received: %s\n", data);
			return;
		}

		uint64_t nodeid = 0;
		if (!hex_to_bytesn(payload, 16, (uint8_t *) &nodeid, true)) {
			printf("[error] Invalid command received: %s\n", data);
			return;
		}

		printf("[invite] Sending invite to node with ID 0x%08X%08X\n",
				(unsigned int) (nodeid >> 32),
				(unsigned int) (nodeid & 0xFFFFFFFF));

		ls_gate_invite(ls, nodeid);
		break;
	}

	case CMD_BROADCAST: {
		/* Number of argument bytes is length of hex digits train except '\n' character */
		int numdigits = strlen(payload) - 1;
		if (numdigits % 2 != 0) {
			printf("[error] Invalid command data: %s, length must be even.\n", data);
			return;
		}

		uint8_t a[UNWDS_MAX_DATA_LEN + 2];
		if (!hex_to_bytesn(payload, numdigits, a, false)) {
			printf("[error] Invalid command received: %s\n", data);
			return;
		}

		ls_gate_broadcast(ls, a, numdigits / 2);

		break;
	}

	case CMD_ADD_STATIC_DEV: {
		/* Minimum length is length of network address in hex + EUI64 in hex + APPID64 in hex + device nonce + no of channel in hex and character '\n' */
		if (strlen(payload) < 16 + 16 + 8 + 8 + 2 + 1) {
			printf("[error] Invalid command received: %s\n", data);
			return;
		}

		uint64_t nodeid = 0;
		if (!hex_to_bytesn(payload, 16, (uint8_t *) &nodeid, true)) {
			printf("[error] Invalid command received: %s\n", data);
			return;
		}

		/* Skip node ID */
		payload += 16;

		uint64_t appid = 0;
		if (!hex_to_bytesn(payload, 16, (uint8_t *) &appid, true)) {
			printf("[error] Invalid command received: %s\n", data);
			return;
		}

		/* Skip app. ID */
		payload += 16;

		ls_addr_t addr = 0;
		if (!hex_to_bytesn(payload, 8, (uint8_t *) &addr, true)) {
			printf("[error] Invalid command received: %s\n", data);
			return;
		}

		if (addr >= LS_GATE_MAX_NODES) {
			printf("[error] Unable to add node with address %u >= %u\n", (unsigned int) addr, (unsigned int) LS_GATE_MAX_NODES);
			return;
		}

		/* Skip address */
		payload += 8;

		uint32_t dev_nonce = 0;
		if (!hex_to_bytesn(payload, 8, (uint8_t *) &dev_nonce, true)) {
			printf("[error] Invalid command received: %s\n", data);
			return;
		}

		/* Skip nonce */
		payload += 8;

		uint8_t channel = 0;
		if (!hex_to_bytesn(payload, 2, (uint8_t *) &channel, true)) {
			printf("[error] Invalid command received: %s\n", data);
			return;
		}

		printf("[gate-commands] Added device: ");
		printf("eui: 0x%08X%08X ",
						(unsigned int) (nodeid >> 32),
						(unsigned int) (nodeid & 0xFFFFFFFF));
		printf("appid: 0x%08X%08X ",
						(unsigned int) (appid >> 32),
						(unsigned int) (appid & 0xFFFFFFFF));

		printf("addr: 0x%08X ", (unsigned int) addr);
		printf("nonce: 0x%08X ", (unsigned int) dev_nonce);
		printf("ch: 0x%02X\n", (unsigned int) channel);

		/* Kick previous device if present */
		if (ls_devlist_is_in_network(devs, addr)) {
			ls_devlist_remove_device(devs, addr);
		}

		/* Add device with specified nonce and address */
		ls_gate_node_t *node = ls_devlist_add_by_addr(devs, addr, nodeid, appid, dev_nonce, &channels[channel]);
		if (node == NULL)
			return;

		node->app_nonce = 0;

		break;
	}

	case CMD_KICK_ALL_STATIC: {
		for (int i = 0; i < LS_GATE_MAX_NODES; i++) {
			if (!devs->nodes_free_list[i]) {
				if (devs->nodes[i].is_static) {
					/* Remove device */
					ls_devlist_remove_device(devs, i);
				}
			}
		}

		puts("[gate-commands] All statically personalized devices are kicked");

		break;
	}

	default:
		printf("[gate-commands] Unsupported: %s\n", data);
		break;
	}
}

void gc_parse_command(ls_gate_t *ls, kernel_pid_t writer, gc_pending_fifo_t *fifo, char *cmd) {
	exec_command(ls, writer, fifo, cmd);
}

#ifdef __cplusplus
}
#endif
