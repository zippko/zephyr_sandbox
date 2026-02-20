// SPDX-License-Identifier: Apache-2.0

#ifndef BLUETOOTH_CTRL_H_
#define BLUETOOTH_CTRL_H_

#include <stdbool.h>
#include <stdint.h>

typedef void (*bt_ctrl_show_passkey_cb_t)(unsigned int passkey);
typedef void (*bt_ctrl_hide_passkey_cb_t)(void);

void bt_ctrl_init(bt_ctrl_show_passkey_cb_t show_passkey_cb,
		  bt_ctrl_hide_passkey_cb_t hide_passkey_cb);
int bt_ctrl_enable_stack_and_start(void);
void bt_ctrl_request_enabled(bool enabled);
bool bt_ctrl_is_ready(void);
bool bt_ctrl_is_connected(void);
int bt_ctrl_send_play_pause(bool play);
int bt_ctrl_send_usage(uint16_t usage);

#endif /* BLUETOOTH_CTRL_H_ */
