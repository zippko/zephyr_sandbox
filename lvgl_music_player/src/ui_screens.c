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

static bool pairing_overlay_is_valid(void)
{
	return (pairing_overlay != NULL) && (pairing_passkey_label != NULL) &&
	       lv_obj_is_valid(pairing_overlay) &&
	       lv_obj_is_valid(pairing_passkey_label);
}

static bool pairing_passkey_allowed(void)
{
	return active_screen == UI_SCREEN_BLUETOOTH;
}

static void pairing_overlay_create_on_active_screen(void)
{
	lv_obj_t *scr = lv_screen_active();
	lv_obj_t *title_label;

	pairing_overlay = lv_obj_create(scr);
	lv_obj_set_size(pairing_overlay, 180, 80);
	lv_obj_align(pairing_overlay, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_radius(pairing_overlay, 12, LV_PART_MAIN);
	lv_obj_set_style_bg_color(pairing_overlay, lv_color_hex(0x000000),
				  LV_PART_MAIN);
	lv_obj_set_style_bg_opa(pairing_overlay, LV_OPA_70, LV_PART_MAIN);
	lv_obj_set_style_border_width(pairing_overlay, 2, LV_PART_MAIN);
	lv_obj_set_style_border_color(pairing_overlay, lv_color_hex(0xE7EEFF),
				      LV_PART_MAIN);
	lv_obj_set_style_pad_all(pairing_overlay, 6, LV_PART_MAIN);
	lv_obj_remove_flag(pairing_overlay, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(pairing_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);

	title_label = lv_label_create(pairing_overlay);
	lv_label_set_text(title_label, "Pairing passkey");
	lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14,
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(title_label, lv_color_hex(0xDCE8F2),
				    LV_PART_MAIN);
	lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 0);

	pairing_passkey_label = lv_label_create(pairing_overlay);
	lv_label_set_text(pairing_passkey_label, "------");
	lv_obj_set_style_text_font(pairing_passkey_label, &lv_font_montserrat_28,
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(pairing_passkey_label, lv_color_hex(0xE7EEFF),
				    LV_PART_MAIN);
	lv_obj_align(pairing_passkey_label, LV_ALIGN_BOTTOM_MID, 0, 0);
}

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
	if (!ui_ready || !pairing_passkey_allowed()) {
		return;
	}

	lvgl_lock();
	if (!pairing_overlay_is_valid()) {
		pairing_overlay = NULL;
		pairing_passkey_label = NULL;
		pairing_overlay_create_on_active_screen();
	}
	lv_label_set_text_fmt(pairing_passkey_label, "%06u", passkey);
	lv_obj_clear_flag(pairing_overlay, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(pairing_overlay);
	lvgl_unlock();
}

void ui_screens_hide_pairing_passkey(void)
{
	if (!ui_ready) {
		return;
	}

	lvgl_lock();
	if (pairing_overlay_is_valid()) {
		lv_obj_add_flag(pairing_overlay, LV_OBJ_FLAG_HIDDEN);
	}
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
