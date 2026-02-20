// SPDX-License-Identifier: Apache-2.0

#ifndef UI_SCREENS_H_
#define UI_SCREENS_H_

#include <stdbool.h>
#include <stdint.h>
#include <lvgl.h>

typedef void (*ui_bt_set_enabled_cb_t)(bool enabled);
typedef bool (*ui_bt_is_enabled_cb_t)(void);

void ui_screens_init(ui_bt_set_enabled_cb_t set_enabled_cb,
		     ui_bt_is_enabled_cb_t is_enabled_cb);
void ui_screens_show_default(void);
void ui_screens_show_pairing_passkey(unsigned int passkey);
void ui_screens_hide_pairing_passkey(void);

void ui_screens_set_pairing_overlay(lv_obj_t *overlay, lv_obj_t *passkey_label);
void ui_screens_clear_pairing_overlay(void);

bool ui_screens_is_bluetooth_enabled(void);
void ui_screens_request_bluetooth_enabled(bool enabled);

#endif /* UI_SCREENS_H_ */
