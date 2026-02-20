// SPDX-License-Identifier: Apache-2.0

#include "screen_bluetooth.h"

#include <lvgl.h>

#include "screen_menu.h"
#include "ui_screens.h"

LV_IMAGE_DECLARE(picture1_bg);

static void bluetooth_switch_event_cb(lv_event_t *e)
{
	lv_obj_t *sw;

	if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
		return;
	}

	sw = lv_event_get_target(e);
	ui_screens_request_bluetooth_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void show_default_screen_async(void *user_data)
{
	ARG_UNUSED(user_data);
	ui_screen_menu_set_focus(UI_MENU_IDX_BLUETOOTH);
	ui_screens_show_default();
}

static void screen_nav_gesture_event_cb(lv_event_t *e)
{
	lv_indev_t *indev = lv_indev_active();

	ARG_UNUSED(e);

	if (indev == NULL) {
		return;
	}

	if (lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) {
		lv_async_call(show_default_screen_async, NULL);
	}
}

void ui_screen_bluetooth_show(void)
{
	lv_obj_t *scr = lv_screen_active();
	lv_obj_t *bg_img;
	lv_obj_t *row;
	lv_obj_t *label;
	lv_obj_t *sw;

	lv_obj_clean(scr);
	ui_screens_clear_pairing_overlay();

	lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
	lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
	lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, LV_PART_MAIN);
	lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
	lv_obj_add_event_cb(scr, screen_nav_gesture_event_cb, LV_EVENT_GESTURE, NULL);

	bg_img = lv_image_create(scr);
	lv_image_set_src(bg_img, &picture1_bg);
	lv_obj_set_size(bg_img, lv_pct(100), lv_pct(100));
	lv_image_set_inner_align(bg_img, LV_IMAGE_ALIGN_COVER);
	lv_obj_set_style_image_opa(bg_img, LV_OPA_50, LV_PART_MAIN);
	lv_obj_center(bg_img);

	row = lv_obj_create(scr);
	lv_obj_set_size(row, 160, 56);
	lv_obj_align(row, LV_ALIGN_CENTER, 0, 8);
	lv_obj_set_style_radius(row, 14, LV_PART_MAIN);
	lv_obj_set_style_bg_color(row, lv_color_hex(0x000000), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_PART_MAIN);
	lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
	lv_obj_set_style_border_color(row, lv_color_hex(0x6F839A), LV_PART_MAIN);
	lv_obj_set_style_pad_hor(row, 12, LV_PART_MAIN);
	lv_obj_set_style_pad_ver(row, 10, LV_PART_MAIN);
	lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

	label = lv_label_create(row);
	lv_label_set_text(label, "Bluetooth");
	lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_style_text_color(label, lv_color_hex(0xDCE8F2), LV_PART_MAIN);
	lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

	sw = lv_switch_create(row);
	if (ui_screens_is_bluetooth_enabled()) {
		lv_obj_add_state(sw, LV_STATE_CHECKED);
	} else {
		lv_obj_remove_state(sw, LV_STATE_CHECKED);
	}
	lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
	lv_obj_add_event_cb(sw, bluetooth_switch_event_cb, LV_EVENT_VALUE_CHANGED,
			    NULL);
}
