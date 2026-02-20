// SPDX-License-Identifier: Apache-2.0

#include "ui_screens.h"

#include <lvgl_zephyr.h>

#include "screen_menu.h"

static ui_bt_set_enabled_cb_t bt_set_enabled_cb;
static ui_bt_is_enabled_cb_t bt_is_enabled_cb;
static lv_obj_t *pairing_overlay;
static lv_obj_t *pairing_passkey_label;
static bool ui_ready;
static enum ui_screen_id active_screen = UI_SCREEN_MENU;

void ui_screens_init(ui_bt_set_enabled_cb_t set_enabled,
		     ui_bt_is_enabled_cb_t is_enabled)
{
	bt_set_enabled_cb = set_enabled;
	bt_is_enabled_cb = is_enabled;
}

void ui_screens_show_default(void)
{
	active_screen = UI_SCREEN_MENU;
	ui_screen_menu_build(lv_screen_active(), true);
	ui_ready = true;
}

void ui_screens_show_pairing_passkey(unsigned int passkey)
{
	if (!ui_ready || pairing_overlay == NULL || pairing_passkey_label == NULL) {
		return;
	}

	lvgl_lock();
	lv_label_set_text_fmt(pairing_passkey_label, "%06u", passkey);
	lv_obj_clear_flag(pairing_overlay, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(pairing_overlay);
	lvgl_unlock();
}

void ui_screens_hide_pairing_passkey(void)
{
	if (!ui_ready || pairing_overlay == NULL) {
		return;
	}

	lvgl_lock();
	lv_obj_add_flag(pairing_overlay, LV_OBJ_FLAG_HIDDEN);
	lvgl_unlock();
}

void ui_screens_set_pairing_overlay(lv_obj_t *overlay, lv_obj_t *passkey_label)
{
	pairing_overlay = overlay;
	pairing_passkey_label = passkey_label;
}

void ui_screens_clear_pairing_overlay(void)
{
	pairing_overlay = NULL;
	pairing_passkey_label = NULL;
}

bool ui_screens_is_bluetooth_enabled(void)
{
	return (bt_is_enabled_cb != NULL) ? bt_is_enabled_cb() : false;
}

void ui_screens_request_bluetooth_enabled(bool enabled)
{
	if (bt_set_enabled_cb == NULL) {
		return;
	}

	bt_set_enabled_cb(enabled);
}

void ui_screens_set_active(enum ui_screen_id screen_id)
{
	active_screen = screen_id;
}

enum ui_screen_id ui_screens_get_active(void)
{
	return active_screen;
}
