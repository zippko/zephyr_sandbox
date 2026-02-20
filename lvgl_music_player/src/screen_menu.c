// SPDX-License-Identifier: Apache-2.0

#include "screen_menu.h"

#include <zephyr/kernel.h>
#include <lvgl.h>

#include "screen_bluetooth.h"
#include "screen_music_player.h"
#include "ui_scale.h"
#include "ui_screens.h"

LV_IMAGE_DECLARE(picture1_bg);

#define RADIAL_MENU_ITEMS_COUNT 6
#define RADIAL_MENU_ITEM_SIZE 50
#define RADIAL_MENU_ZOOM_NORMAL 256
#define RADIAL_MENU_ZOOM_ACTIVE 307
#define GESTURE_RATE_LIMIT_MS 350
#define MENU_LABEL_FADE_MS 160

struct radial_menu_item {
	const char *symbol;
	const char *label;
};

static const struct radial_menu_item radial_menu_items[RADIAL_MENU_ITEMS_COUNT] = {
	{ LV_SYMBOL_AUDIO, "Music" },
	{ LV_SYMBOL_VIDEO, "Video" },
	{ LV_SYMBOL_SETTINGS, "Settings" },
	{ LV_SYMBOL_BLUETOOTH, "Bluetooth" },
	{ LV_SYMBOL_FILE, "Storage" },
	{ LV_SYMBOL_POWER, "Power" },
};

static lv_obj_t *radial_menu_items_obj[RADIAL_MENU_ITEMS_COUNT];
static lv_obj_t *radial_menu_symbols_obj[RADIAL_MENU_ITEMS_COUNT];
static lv_obj_t *radial_menu_center_label;
static uint8_t radial_menu_active_idx;
static int32_t radial_menu_item_zoom[RADIAL_MENU_ITEMS_COUNT];
static int64_t last_gesture_action_ms;
static uint8_t radial_menu_label_idx;
static bool radial_menu_label_animating;
static bool radial_menu_label_pending;
static uint8_t radial_menu_label_pending_idx;

static bool radial_menu_is_ready(void)
{
	if ((radial_menu_center_label == NULL) ||
	    !lv_obj_is_valid(radial_menu_center_label)) {
		return false;
	}

	for (size_t i = 0; i < RADIAL_MENU_ITEMS_COUNT; i++) {
		if ((radial_menu_items_obj[i] == NULL) ||
		    (radial_menu_symbols_obj[i] == NULL) ||
		    !lv_obj_is_valid(radial_menu_items_obj[i]) ||
		    !lv_obj_is_valid(radial_menu_symbols_obj[i])) {
			return false;
		}
	}

	return true;
}

void ui_screen_menu_set_focus(uint8_t idx)
{
	radial_menu_active_idx = idx;
}

uint8_t ui_screen_menu_get_focus(void)
{
	return radial_menu_active_idx;
}

static void radial_menu_zoom_exec_cb(void *var, int32_t v)
{
	lv_obj_set_style_transform_zoom((lv_obj_t *)var, v, LV_PART_MAIN);
}

static void radial_menu_label_fade_exec_cb(void *var, int32_t v)
{
	ARG_UNUSED(var);
	lv_obj_set_style_text_opa(radial_menu_center_label, (lv_opa_t)v, LV_PART_MAIN);
}

static void radial_menu_start_label_animation(uint8_t target_idx);

static void radial_menu_label_fade_in_ready_cb(lv_anim_t *a)
{
	ARG_UNUSED(a);

	radial_menu_label_animating = false;
	if (radial_menu_label_pending &&
	    radial_menu_label_pending_idx != radial_menu_label_idx) {
		uint8_t next_idx = radial_menu_label_pending_idx;

		radial_menu_label_pending = false;
		radial_menu_start_label_animation(next_idx);
	}
}

static void radial_menu_label_fade_out_ready_cb(lv_anim_t *a)
{
	lv_anim_t fade_in_anim;

	ARG_UNUSED(a);

	radial_menu_label_idx = radial_menu_label_pending_idx;
	lv_label_set_text(radial_menu_center_label,
			  radial_menu_items[radial_menu_label_idx].label);

	lv_anim_init(&fade_in_anim);
	lv_anim_set_var(&fade_in_anim, NULL);
	lv_anim_set_values(&fade_in_anim, LV_OPA_TRANSP, LV_OPA_COVER);
	lv_anim_set_time(&fade_in_anim, MENU_LABEL_FADE_MS);
	lv_anim_set_exec_cb(&fade_in_anim, radial_menu_label_fade_exec_cb);
	lv_anim_set_ready_cb(&fade_in_anim, radial_menu_label_fade_in_ready_cb);
	lv_anim_start(&fade_in_anim);
}

static void radial_menu_start_label_animation(uint8_t target_idx)
{
	lv_anim_t fade_out_anim;

	if (!radial_menu_is_ready()) {
		return;
	}

	if (radial_menu_label_animating) {
		radial_menu_label_pending = true;
		radial_menu_label_pending_idx = target_idx;
		return;
	}

	if (target_idx == radial_menu_label_idx) {
		return;
	}

	radial_menu_label_animating = true;
	radial_menu_label_pending = true;
	radial_menu_label_pending_idx = target_idx;

	lv_anim_init(&fade_out_anim);
	lv_anim_set_var(&fade_out_anim, NULL);
	lv_anim_set_values(&fade_out_anim, LV_OPA_COVER, LV_OPA_TRANSP);
	lv_anim_set_time(&fade_out_anim, MENU_LABEL_FADE_MS);
	lv_anim_set_exec_cb(&fade_out_anim, radial_menu_label_fade_exec_cb);
	lv_anim_set_ready_cb(&fade_out_anim, radial_menu_label_fade_out_ready_cb);
	lv_anim_start(&fade_out_anim);
}

static void radial_menu_refresh(bool animate_label)
{
	lv_point_t slots[RADIAL_MENU_ITEMS_COUNT] = {
		{ 0, ui_scale_px(-74) },
		{ ui_scale_px(64), ui_scale_px(-37) },
		{ ui_scale_px(64), ui_scale_px(37) },
		{ 0, ui_scale_px(74) },
		{ ui_scale_px(-64), ui_scale_px(37) },
		{ ui_scale_px(-64), ui_scale_px(-37) },
	};
	int32_t border_w = ui_scale_px(1);

	if (!radial_menu_is_ready()) {
		return;
	}
	if (border_w < 1) {
		border_w = 1;
	}

	for (size_t i = 0; i < RADIAL_MENU_ITEMS_COUNT; i++) {
		bool active = (i == radial_menu_active_idx);
		lv_obj_t *obj = radial_menu_items_obj[i];
		int32_t from_zoom = radial_menu_item_zoom[i];
		int32_t to_zoom = active ? RADIAL_MENU_ZOOM_ACTIVE :
					  RADIAL_MENU_ZOOM_NORMAL;
		lv_anim_t anim;

		lv_anim_init(&anim);
		lv_anim_set_var(&anim, obj);
		lv_anim_set_values(&anim, from_zoom, to_zoom);
		lv_anim_set_time(&anim, 160);
		lv_anim_set_exec_cb(&anim, radial_menu_zoom_exec_cb);
		lv_anim_start(&anim);
		radial_menu_item_zoom[i] = to_zoom;

		lv_obj_align(obj, LV_ALIGN_CENTER, slots[i].x, slots[i].y);
		lv_obj_set_style_bg_opa(obj, active ? LV_OPA_TRANSP : LV_OPA_30,
					LV_PART_MAIN);
		lv_obj_set_style_bg_color(obj, lv_color_hex(0x12202E), LV_PART_MAIN);
		lv_obj_set_style_border_width(obj, border_w, LV_PART_MAIN);
		lv_obj_set_style_border_color(obj,
					      active ? lv_color_hex(0xE7EEFF) :
						       lv_color_hex(0x6F839A),
					      LV_PART_MAIN);
		lv_obj_set_style_text_color(radial_menu_symbols_obj[i],
					    active ? lv_color_hex(0xFFFFFF) :
						     lv_color_hex(0xD0DEEB),
					    LV_PART_MAIN);
	}

	if (animate_label) {
		radial_menu_start_label_animation(radial_menu_active_idx);
	} else {
		radial_menu_label_idx = radial_menu_active_idx;
		radial_menu_label_animating = false;
		radial_menu_label_pending = false;
		lv_obj_set_style_text_opa(radial_menu_center_label, LV_OPA_COVER,
					  LV_PART_MAIN);
		lv_label_set_text(radial_menu_center_label,
				  radial_menu_items[radial_menu_active_idx].label);
	}
}

static void default_screen_gesture_event_cb(lv_event_t *e)
{
	lv_indev_t *indev = lv_indev_active();
	lv_dir_t gesture_dir;
	int64_t now_ms;

	ARG_UNUSED(e);

	if (ui_screens_get_active() != UI_SCREEN_MENU) {
		return;
	}

	if (indev == NULL) {
		return;
	}
	if (!radial_menu_is_ready()) {
		return;
	}

	now_ms = k_uptime_get();
	if ((now_ms - last_gesture_action_ms) < GESTURE_RATE_LIMIT_MS) {
		return;
	}

	gesture_dir = lv_indev_get_gesture_dir(indev);
	if (gesture_dir == LV_DIR_LEFT) {
		radial_menu_active_idx =
			(uint8_t)((radial_menu_active_idx + 1) % RADIAL_MENU_ITEMS_COUNT);
		last_gesture_action_ms = now_ms;
		radial_menu_refresh(true);
	} else if (gesture_dir == LV_DIR_RIGHT) {
		radial_menu_active_idx = (uint8_t)((radial_menu_active_idx +
						    RADIAL_MENU_ITEMS_COUNT - 1) %
						   RADIAL_MENU_ITEMS_COUNT);
		last_gesture_action_ms = now_ms;
		radial_menu_refresh(true);
	}
}

static void radial_menu_item_event_cb(lv_event_t *e)
{
	lv_obj_t *target;

	if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
		return;
	}

	target = lv_event_get_target(e);
	for (uint8_t i = 0; i < RADIAL_MENU_ITEMS_COUNT; i++) {
		if (radial_menu_items_obj[i] != target) {
			continue;
		}

		if (radial_menu_active_idx != i) {
			radial_menu_active_idx = i;
			radial_menu_refresh(true);
		} else if (i == UI_MENU_IDX_MUSIC) {
			ui_screen_music_player_show();
		} else if (i == UI_MENU_IDX_BLUETOOTH) {
			ui_screen_bluetooth_show();
		}
		break;
	}
}

void ui_screen_menu_build(lv_obj_t *scr, bool clean_first)
{
	lv_obj_t *bg_img;
	lv_obj_t *menu_ring;
	lv_obj_t *pairing_overlay;
	lv_obj_t *pairing_passkey_label;
	int32_t item_size;

	if (clean_first) {
		lv_obj_clean(scr);
	}
	ui_scale_refresh_for_active_screen();
	ui_screens_set_active(UI_SCREEN_MENU);
	ui_screens_clear_pairing_overlay();

	lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
	lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
	lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, LV_PART_MAIN);
	lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
	lv_obj_add_event_cb(scr, default_screen_gesture_event_cb, LV_EVENT_GESTURE,
			    NULL);

	bg_img = lv_image_create(scr);
	lv_image_set_src(bg_img, &picture1_bg);
	lv_obj_set_size(bg_img, lv_pct(100), lv_pct(100));
	lv_image_set_inner_align(bg_img, LV_IMAGE_ALIGN_COVER);
	lv_obj_set_style_image_opa(bg_img, LV_OPA_50, LV_PART_MAIN);
	lv_obj_add_flag(bg_img, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_center(bg_img);

	menu_ring = lv_obj_create(scr);
	lv_obj_set_size(menu_ring, ui_scale_px(200), ui_scale_px(200));
	lv_obj_center(menu_ring);
	lv_obj_set_style_radius(menu_ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(menu_ring, LV_OPA_10, LV_PART_MAIN);
	lv_obj_set_style_bg_color(menu_ring, lv_color_hex(0x000000), LV_PART_MAIN);
	lv_obj_set_style_border_width(menu_ring, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(menu_ring, 0, LV_PART_MAIN);
	lv_obj_add_flag(menu_ring, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_remove_flag(menu_ring, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_remove_flag(menu_ring, LV_OBJ_FLAG_CLICKABLE);

	for (size_t i = 0; i < RADIAL_MENU_ITEMS_COUNT; i++) {
		item_size = ui_scale_px(RADIAL_MENU_ITEM_SIZE);
		radial_menu_items_obj[i] = lv_obj_create(scr);
		lv_obj_set_size(radial_menu_items_obj[i], item_size, item_size);
		lv_obj_set_style_radius(radial_menu_items_obj[i], LV_RADIUS_CIRCLE,
					LV_PART_MAIN);
		lv_obj_set_style_bg_color(radial_menu_items_obj[i],
					  lv_color_hex(0x12202E), LV_PART_MAIN);
		lv_obj_set_style_pad_all(radial_menu_items_obj[i], 0, LV_PART_MAIN);
		lv_obj_set_style_transform_zoom(radial_menu_items_obj[i],
						RADIAL_MENU_ZOOM_NORMAL,
						LV_PART_MAIN);
		radial_menu_item_zoom[i] = RADIAL_MENU_ZOOM_NORMAL;
		lv_obj_add_flag(radial_menu_items_obj[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
		lv_obj_remove_flag(radial_menu_items_obj[i], LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_event_cb(radial_menu_items_obj[i], radial_menu_item_event_cb,
				    LV_EVENT_CLICKED, NULL);

		radial_menu_symbols_obj[i] = lv_label_create(radial_menu_items_obj[i]);
		lv_label_set_text(radial_menu_symbols_obj[i], radial_menu_items[i].symbol);
		lv_obj_set_style_text_font(radial_menu_symbols_obj[i],
					   ui_scale_font_montserrat(16),
					   LV_PART_MAIN);
		lv_obj_add_flag(radial_menu_symbols_obj[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
		lv_obj_center(radial_menu_symbols_obj[i]);
	}

	radial_menu_center_label = lv_label_create(scr);
	lv_obj_set_style_text_font(radial_menu_center_label,
				   ui_scale_font_montserrat(16), LV_PART_MAIN);
	lv_obj_set_style_text_color(radial_menu_center_label, lv_color_hex(0xF0F4F8),
				    LV_PART_MAIN);
	lv_obj_align(radial_menu_center_label, LV_ALIGN_CENTER, 0, ui_scale_px(2));

	if (radial_menu_active_idx >= RADIAL_MENU_ITEMS_COUNT) {
		radial_menu_active_idx = 0;
	}
	last_gesture_action_ms = 0;
	radial_menu_refresh(false);

	pairing_overlay = lv_obj_create(scr);
	lv_obj_set_size(pairing_overlay, ui_scale_px(180), ui_scale_px(80));
	lv_obj_align(pairing_overlay, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_radius(pairing_overlay, ui_scale_px(12), LV_PART_MAIN);
	lv_obj_set_style_bg_color(pairing_overlay, lv_color_hex(0x000000),
				  LV_PART_MAIN);
	lv_obj_set_style_bg_opa(pairing_overlay, LV_OPA_70, LV_PART_MAIN);
	lv_obj_set_style_border_width(pairing_overlay, ui_scale_px(2), LV_PART_MAIN);
	lv_obj_set_style_border_color(pairing_overlay, lv_color_hex(0xE7EEFF),
				      LV_PART_MAIN);
	lv_obj_set_style_pad_all(pairing_overlay, ui_scale_px(6), LV_PART_MAIN);
	lv_obj_remove_flag(pairing_overlay, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(pairing_overlay, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(pairing_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);

	lv_obj_t *pairing_title_label = lv_label_create(pairing_overlay);
	lv_label_set_text(pairing_title_label, "Pairing passkey");
	lv_obj_set_style_text_font(pairing_title_label, ui_scale_font_montserrat(14),
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(pairing_title_label, lv_color_hex(0xDCE8F2),
				    LV_PART_MAIN);
	lv_obj_align(pairing_title_label, LV_ALIGN_TOP_MID, 0, 0);

	pairing_passkey_label = lv_label_create(pairing_overlay);
	lv_label_set_text(pairing_passkey_label, "------");
	lv_obj_set_style_text_font(pairing_passkey_label, ui_scale_font_montserrat(28),
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(pairing_passkey_label, lv_color_hex(0xE7EEFF),
				    LV_PART_MAIN);
	lv_obj_align(pairing_passkey_label, LV_ALIGN_BOTTOM_MID, 0, 0);

	ui_screens_set_pairing_overlay(pairing_overlay, pairing_passkey_label);
}
