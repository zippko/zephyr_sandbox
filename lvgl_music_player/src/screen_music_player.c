// SPDX-License-Identifier: Apache-2.0

#include "screen_music_player.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <lvgl.h>

#include "screen_menu.h"
#include "ui_scale.h"
#include "ui_screens.h"

LV_IMAGE_DECLARE(picture1_bg);

LOG_MODULE_REGISTER(screen_music_player, LOG_LEVEL_INF);

#define PROGRESS_MAX 100
#define TIMER_PERIOD_MS 1000
#define SONG_FADE_MS 160
#define VOLUME_HOLD_ENABLE_DELAY_MS 1200
#define VOLUME_CMD_INTERVAL_MS 140

#define HID_CONSUMER_SCAN_NEXT 0x00B5
#define HID_CONSUMER_SCAN_PREV 0x00B6
#define HID_CONSUMER_VOL_UP 0x00E9
#define HID_CONSUMER_VOL_DOWN 0x00EA

static ui_music_send_play_pause_cb_t send_play_pause_cb;
static ui_music_send_usage_cb_t send_usage_cb;
static ui_music_bt_connected_cb_t bt_connected_cb;

static lv_obj_t *progress_arc;
static lv_obj_t *elapsed_label;
static lv_obj_t *play_icon_label;
static lv_obj_t *next_icon_label;
static lv_obj_t *prev_icon_label;
static lv_obj_t *title_label;
static lv_obj_t *artist_label;
static lv_obj_t *duration_label;
static lv_obj_t *volume_overlay;
static lv_obj_t *volume_label;
static lv_timer_t *progress_timer;

struct song_info {
	const char *title;
	const char *artist;
	uint16_t duration_sec;
};

static const struct song_info songs[] = {
	{ "Track One", "Echo Harbor", 173U },
	{ "Track Two", "Echo Harbor", 149U },
	{ "Track Three", "Echo Harbor", 132U },
	{ "Track Four", "Echo Harbor", 164U },
	{ "Track Five", "Echo Harbor", 121U },
};

static uint32_t elapsed_sec;
static size_t current_song_index;
static bool is_playing;
static bool song_change_animating;
static int32_t queued_song_steps;
static int8_t active_song_step;
static bool volume_hold_active;
static int16_t volume_hold_last_y;
static int16_t volume_swipe_step_px;
static int64_t volume_hold_enable_at_ms;
static int64_t last_volume_cmd_ms;

void ui_screen_music_player_init(ui_music_send_play_pause_cb_t play_pause_cb,
				 ui_music_send_usage_cb_t usage_cb,
				 ui_music_bt_connected_cb_t connected_cb)
{
	send_play_pause_cb = play_pause_cb;
	send_usage_cb = usage_cb;
	bt_connected_cb = connected_cb;
}

static bool bt_connected(void)
{
	return (bt_connected_cb != NULL) && bt_connected_cb();
}

static void update_media_controls_state(void)
{
	bool connected = bt_connected();
	lv_color_t color = connected ? lv_color_hex(0xE7EEFF) :
				       lv_color_hex(0x6F839A);

	if (!lv_obj_is_valid(play_icon_label) || !lv_obj_is_valid(next_icon_label) ||
	    !lv_obj_is_valid(prev_icon_label)) {
		return;
	}

	if (connected) {
		lv_obj_add_flag(play_icon_label, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(next_icon_label, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(prev_icon_label, LV_OBJ_FLAG_CLICKABLE);
	} else {
		lv_obj_remove_flag(play_icon_label, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_remove_flag(next_icon_label, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_remove_flag(prev_icon_label, LV_OBJ_FLAG_CLICKABLE);
	}

	lv_obj_set_style_text_color(play_icon_label, color, LV_PART_MAIN);
	lv_obj_set_style_text_color(next_icon_label, color, LV_PART_MAIN);
	lv_obj_set_style_text_color(prev_icon_label, color, LV_PART_MAIN);
}

static int send_play_pause(bool play)
{
	if (send_play_pause_cb == NULL) {
		return -ENOTSUP;
	}
	return send_play_pause_cb(play);
}

static int send_usage(uint16_t usage)
{
	if (send_usage_cb == NULL) {
		return -ENOTSUP;
	}
	return send_usage_cb(usage);
}

static void update_progress_label(uint32_t sec)
{
	lv_label_set_text_fmt(elapsed_label, "%u:%02u", sec / 60U, sec % 60U);
}

static void update_song_labels(void)
{
	uint16_t duration_sec = songs[current_song_index].duration_sec;

	lv_label_set_text(title_label, songs[current_song_index].title);
	lv_label_set_text(artist_label, songs[current_song_index].artist);
	lv_label_set_text_fmt(duration_label, "%u:%02u", duration_sec / 60U,
			      duration_sec % 60U);
	lv_obj_align(title_label, LV_ALIGN_CENTER, 0, ui_scale_px(-15));
	lv_obj_align_to(artist_label, title_label, LV_ALIGN_OUT_BOTTOM_MID, 0,
			ui_scale_px(1));
}

static void reset_song_progress(void)
{
	elapsed_sec = 0U;
	lv_arc_set_value(progress_arc, 0);
	update_progress_label(0U);
}

static void apply_song_step(int8_t step)
{
	if (step > 0) {
		current_song_index = (current_song_index + 1U) % ARRAY_SIZE(songs);
	} else if (step < 0) {
		if (current_song_index == 0U) {
			current_song_index = ARRAY_SIZE(songs) - 1U;
		} else {
			current_song_index--;
		}
	}

	update_song_labels();
	reset_song_progress();
}

static void song_fade_exec_cb(void *var, int32_t v)
{
	ARG_UNUSED(var);
	lv_obj_set_style_text_opa(title_label, (lv_opa_t)v, LV_PART_MAIN);
	lv_obj_set_style_text_opa(artist_label, (lv_opa_t)v, LV_PART_MAIN);
}

static void show_volume_overlay(char symbol)
{
	char text[2] = { symbol, '\0' };

	if (!lv_obj_is_valid(volume_overlay) || !lv_obj_is_valid(volume_label)) {
		return;
	}

	lv_label_set_text(volume_label, text);
	lv_obj_clear_flag(volume_overlay, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(volume_overlay);
}

static void hide_volume_overlay(void)
{
	if (!lv_obj_is_valid(volume_overlay)) {
		return;
	}

	lv_obj_add_flag(volume_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void hold_volume_event_cb(lv_event_t *e)
{
	lv_indev_t *indev = lv_indev_active();
	lv_point_t p;
	int16_t dy;
	int err;
	int64_t now_ms;

	if (!lv_obj_is_valid(volume_overlay) || !lv_obj_is_valid(volume_label)) {
		volume_hold_active = false;
		return;
	}

	switch (lv_event_get_code(e)) {
	case LV_EVENT_LONG_PRESSED:
		if (k_uptime_get() < volume_hold_enable_at_ms) {
			break;
		}
		volume_hold_active = true;
		show_volume_overlay(' ');
		if (indev != NULL) {
			lv_indev_get_point(indev, &p);
			volume_hold_last_y = p.y;
		}
		break;
	case LV_EVENT_PRESSING:
		if (!volume_hold_active || indev == NULL) {
			break;
		}

		lv_indev_get_point(indev, &p);
		dy = p.y - volume_hold_last_y;

		while (dy <= -volume_swipe_step_px) {
			now_ms = k_uptime_get();
			if ((now_ms - last_volume_cmd_ms) < VOLUME_CMD_INTERVAL_MS) {
				break;
			}

			err = send_usage(HID_CONSUMER_VOL_UP);
			if (err && err != -ENOTCONN) {
				LOG_WRN("Failed to send media command 'vol_up' (err %d)", err);
				break;
			}

			last_volume_cmd_ms = now_ms;
			show_volume_overlay('+');
			volume_hold_last_y -= volume_swipe_step_px;
			dy += volume_swipe_step_px;
		}

		while (dy >= volume_swipe_step_px) {
			now_ms = k_uptime_get();
			if ((now_ms - last_volume_cmd_ms) < VOLUME_CMD_INTERVAL_MS) {
				break;
			}

			err = send_usage(HID_CONSUMER_VOL_DOWN);
			if (err && err != -ENOTCONN) {
				LOG_WRN("Failed to send media command 'vol_down' (err %d)", err);
				break;
			}

			last_volume_cmd_ms = now_ms;
			show_volume_overlay('-');
			volume_hold_last_y += volume_swipe_step_px;
			dy -= volume_swipe_step_px;
		}
		break;
	case LV_EVENT_RELEASED:
		volume_hold_active = false;
		hide_volume_overlay();
		break;
	case LV_EVENT_PRESS_LOST:
		if (indev != NULL && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
			break;
		}
		volume_hold_active = false;
		hide_volume_overlay();
		break;
	default:
		break;
	}
}

static void add_hold_volume_events(lv_obj_t *obj)
{
	lv_obj_add_event_cb(obj, hold_volume_event_cb, LV_EVENT_LONG_PRESSED, NULL);
	lv_obj_add_event_cb(obj, hold_volume_event_cb, LV_EVENT_PRESSING, NULL);
	lv_obj_add_event_cb(obj, hold_volume_event_cb, LV_EVENT_RELEASED, NULL);
	lv_obj_add_event_cb(obj, hold_volume_event_cb, LV_EVENT_PRESS_LOST, NULL);
}

static void start_song_change_animation(void);

static void song_fade_in_ready_cb(lv_anim_t *a)
{
	ARG_UNUSED(a);
	song_change_animating = false;
	start_song_change_animation();
}

static void song_fade_out_ready_cb(lv_anim_t *a)
{
	lv_anim_t fade_in_anim;

	ARG_UNUSED(a);
	apply_song_step(active_song_step);

	lv_anim_init(&fade_in_anim);
	lv_anim_set_var(&fade_in_anim, NULL);
	lv_anim_set_values(&fade_in_anim, LV_OPA_TRANSP, LV_OPA_COVER);
	lv_anim_set_time(&fade_in_anim, SONG_FADE_MS);
	lv_anim_set_exec_cb(&fade_in_anim, song_fade_exec_cb);
	lv_anim_set_ready_cb(&fade_in_anim, song_fade_in_ready_cb);
	lv_anim_start(&fade_in_anim);
}

static void start_song_change_animation(void)
{
	lv_anim_t fade_out_anim;

	if (song_change_animating || queued_song_steps == 0) {
		return;
	}

	song_change_animating = true;
	if (queued_song_steps > 0) {
		active_song_step = 1;
		queued_song_steps--;
	} else {
		active_song_step = -1;
		queued_song_steps++;
	}

	lv_anim_init(&fade_out_anim);
	lv_anim_set_var(&fade_out_anim, NULL);
	lv_anim_set_values(&fade_out_anim, LV_OPA_COVER, LV_OPA_TRANSP);
	lv_anim_set_time(&fade_out_anim, SONG_FADE_MS);
	lv_anim_set_exec_cb(&fade_out_anim, song_fade_exec_cb);
	lv_anim_set_ready_cb(&fade_out_anim, song_fade_out_ready_cb);
	lv_anim_start(&fade_out_anim);
}

static void progress_timer_cb(lv_timer_t *timer)
{
	uint16_t duration_sec;
	uint16_t progress_value;

	ARG_UNUSED(timer);
	update_media_controls_state();

	/* The active screen can be rebuilt while this timer is running. */
	if (!lv_obj_is_valid(progress_arc) || !lv_obj_is_valid(elapsed_label)) {
		if (progress_timer != NULL) {
			lv_timer_delete(progress_timer);
			progress_timer = NULL;
		}
		return;
	}

	if (!is_playing) {
		return;
	}

	duration_sec = songs[current_song_index].duration_sec;
	elapsed_sec = (elapsed_sec + 1U) % (duration_sec + 1U);
	progress_value = (uint16_t)((elapsed_sec * PROGRESS_MAX) / duration_sec);
	lv_arc_set_value(progress_arc, progress_value);
	update_progress_label(elapsed_sec);
}

static void play_icon_event_cb(lv_event_t *e)
{
	int err;

	ARG_UNUSED(e);

	if (!bt_connected()) {
		return;
	}

	is_playing = !is_playing;
	lv_label_set_text(play_icon_label,
			  is_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

	err = send_play_pause(is_playing);
	if (err) {
		LOG_WRN("Failed to send media command '%s' (err %d)",
			is_playing ? "play" : "pause", err);
	}
}

static void skip_to_next_song(void)
{
	int err;

	if (!bt_connected()) {
		return;
	}

	queued_song_steps++;
	start_song_change_animation();

	err = send_usage(HID_CONSUMER_SCAN_NEXT);
	if (err) {
		LOG_WRN("Failed to send media command 'next' (err %d)", err);
	}
}

static void skip_to_prev_song(void)
{
	int err;

	if (!bt_connected()) {
		return;
	}

	queued_song_steps--;
	start_song_change_animation();

	err = send_usage(HID_CONSUMER_SCAN_PREV);
	if (err) {
		LOG_WRN("Failed to send media command 'previous' (err %d)", err);
	}
}

static void next_song_event_cb(lv_event_t *e)
{
	ARG_UNUSED(e);
	skip_to_next_song();
}

static void prev_song_event_cb(lv_event_t *e)
{
	ARG_UNUSED(e);
	skip_to_prev_song();
}

static void show_default_screen_async(void *user_data)
{
	ARG_UNUSED(user_data);
	ui_screen_menu_set_focus(UI_MENU_IDX_MUSIC);
	ui_screens_show_default();
}

static void screen_nav_gesture_event_cb(lv_event_t *e)
{
	lv_indev_t *indev = lv_indev_active();

	ARG_UNUSED(e);

	if (ui_screens_get_active() != UI_SCREEN_MUSIC_PLAYER) {
		return;
	}

	if (indev == NULL) {
		return;
	}

	if (lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) {
		lv_async_call(show_default_screen_async, NULL);
	}
}

void ui_screen_music_player_show(void)
{
	lv_obj_t *scr = lv_screen_active();
	lv_obj_t *bg_img;
	lv_obj_t *separator;
	int32_t arc_size;
	int32_t arc_width;

	lv_obj_clean(scr);
	ui_scale_refresh_for_active_screen();
	ui_screens_set_active(UI_SCREEN_MUSIC_PLAYER);
	lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
	lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
	lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, LV_PART_MAIN);
	lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
	lv_obj_add_event_cb(scr, screen_nav_gesture_event_cb, LV_EVENT_GESTURE, NULL);
	add_hold_volume_events(scr);

	bg_img = lv_image_create(scr);
	lv_image_set_src(bg_img, &picture1_bg);
	lv_obj_set_size(bg_img, lv_pct(100), lv_pct(100));
	lv_image_set_inner_align(bg_img, LV_IMAGE_ALIGN_COVER);
	lv_obj_set_style_image_opa(bg_img, LV_OPA_50, LV_PART_MAIN);
	lv_obj_add_flag(bg_img, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_center(bg_img);
	add_hold_volume_events(bg_img);

	arc_size = ui_scale_px(214);
	arc_width = ui_scale_px(8);
	if (arc_width < 1) {
		arc_width = 1;
	}

	progress_arc = lv_arc_create(scr);
	lv_obj_set_size(progress_arc, arc_size, arc_size);
	lv_obj_center(progress_arc);
	lv_arc_set_range(progress_arc, 0, PROGRESS_MAX);
	lv_arc_set_bg_angles(progress_arc, 0, 360);
	lv_arc_set_rotation(progress_arc, 270);
	lv_arc_set_mode(progress_arc, LV_ARC_MODE_NORMAL);
	lv_arc_set_value(progress_arc, 0);
	lv_obj_set_style_arc_width(progress_arc, arc_width, LV_PART_MAIN);
	lv_obj_set_style_arc_opa(progress_arc, LV_OPA_TRANSP, LV_PART_MAIN);
	lv_obj_set_style_arc_width(progress_arc, arc_width, LV_PART_INDICATOR);
	lv_obj_set_style_arc_color(progress_arc, lv_color_hex(0x021E57),
			       LV_PART_INDICATOR);
	lv_obj_set_style_arc_rounded(progress_arc, true, LV_PART_INDICATOR);
	lv_obj_set_style_bg_opa(progress_arc, LV_OPA_TRANSP, LV_PART_KNOB);
	lv_obj_add_flag(progress_arc, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_remove_flag(progress_arc, LV_OBJ_FLAG_CLICKABLE);
	add_hold_volume_events(progress_arc);

	title_label = lv_label_create(scr);
	lv_obj_set_style_text_font(title_label, ui_scale_font_montserrat(16),
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(title_label, lv_color_hex(0xF0F4F8), LV_PART_MAIN);
	lv_obj_add_flag(title_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_align(title_label, LV_ALIGN_CENTER, 0, ui_scale_px(-15));
	add_hold_volume_events(title_label);

	artist_label = lv_label_create(scr);
	lv_obj_set_style_text_font(artist_label, ui_scale_font_montserrat(14),
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(artist_label, lv_color_hex(0x9BB3C9), LV_PART_MAIN);
	lv_obj_add_flag(artist_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_align_to(artist_label, title_label, LV_ALIGN_OUT_BOTTOM_MID, 0,
			ui_scale_px(1));
	add_hold_volume_events(artist_label);

	play_icon_label = lv_label_create(scr);
	lv_label_set_text(play_icon_label, LV_SYMBOL_PLAY);
	lv_obj_set_style_text_font(play_icon_label, ui_scale_font_montserrat(28),
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(play_icon_label, lv_color_hex(0xE7EEFF), LV_PART_MAIN);
	lv_obj_add_flag(play_icon_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_align(play_icon_label, LV_ALIGN_CENTER, 0, ui_scale_px(37));
	lv_obj_add_event_cb(play_icon_label, play_icon_event_cb, LV_EVENT_CLICKED, NULL);
	add_hold_volume_events(play_icon_label);

	next_icon_label = lv_label_create(scr);
	lv_label_set_text(next_icon_label, LV_SYMBOL_NEXT);
	lv_obj_set_style_text_font(next_icon_label, ui_scale_font_montserrat(28),
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(next_icon_label, lv_color_hex(0xE7EEFF), LV_PART_MAIN);
	lv_obj_add_flag(next_icon_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_align_to(next_icon_label, play_icon_label, LV_ALIGN_OUT_RIGHT_MID,
			ui_scale_px(24), 0);
	lv_obj_add_event_cb(next_icon_label, next_song_event_cb, LV_EVENT_CLICKED, NULL);
	add_hold_volume_events(next_icon_label);

	prev_icon_label = lv_label_create(scr);
	lv_label_set_text(prev_icon_label, LV_SYMBOL_PREV);
	lv_obj_set_style_text_font(prev_icon_label, ui_scale_font_montserrat(28),
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(prev_icon_label, lv_color_hex(0xE7EEFF), LV_PART_MAIN);
	lv_obj_add_flag(prev_icon_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_align_to(prev_icon_label, play_icon_label, LV_ALIGN_OUT_LEFT_MID,
			ui_scale_px(-24), 0);
	lv_obj_add_event_cb(prev_icon_label, prev_song_event_cb, LV_EVENT_CLICKED, NULL);
	add_hold_volume_events(prev_icon_label);

	elapsed_label = lv_label_create(scr);
	lv_label_set_text(elapsed_label, "0:00");
	lv_obj_set_style_text_font(elapsed_label, ui_scale_font_montserrat(14),
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(elapsed_label, lv_color_hex(0xDCE8F2), LV_PART_MAIN);
	lv_obj_add_flag(elapsed_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_align(elapsed_label, LV_ALIGN_BOTTOM_MID, ui_scale_px(-22),
		     ui_scale_px(-38));
	add_hold_volume_events(elapsed_label);

	separator = lv_label_create(scr);
	lv_label_set_text(separator, "|");
	lv_obj_set_style_text_font(separator, ui_scale_font_montserrat(14),
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(separator, lv_color_hex(0xDCE8F2), LV_PART_MAIN);
	lv_obj_add_flag(separator, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_align(separator, LV_ALIGN_BOTTOM_MID, 0, ui_scale_px(-38));
	add_hold_volume_events(separator);

	duration_label = lv_label_create(scr);
	lv_label_set_text(duration_label, "3:00");
	lv_obj_set_style_text_font(duration_label, ui_scale_font_montserrat(14),
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(duration_label, lv_color_hex(0xDCE8F2), LV_PART_MAIN);
	lv_obj_add_flag(duration_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_align(duration_label, LV_ALIGN_BOTTOM_MID, ui_scale_px(22),
		     ui_scale_px(-38));
	add_hold_volume_events(duration_label);

	volume_overlay = lv_obj_create(scr);
	lv_obj_set_size(volume_overlay, ui_scale_px(88), ui_scale_px(88));
	lv_obj_align(volume_overlay, LV_ALIGN_CENTER, 0, ui_scale_px(-4));
	lv_obj_set_style_radius(volume_overlay, LV_RADIUS_CIRCLE, LV_PART_MAIN);
	lv_obj_set_style_bg_color(volume_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(volume_overlay, LV_OPA_70, LV_PART_MAIN);
	lv_obj_set_style_border_width(volume_overlay, ui_scale_px(2), LV_PART_MAIN);
	lv_obj_set_style_border_color(volume_overlay, lv_color_hex(0xE7EEFF), LV_PART_MAIN);
	lv_obj_set_style_pad_all(volume_overlay, 0, LV_PART_MAIN);
	lv_obj_remove_flag(volume_overlay, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(volume_overlay, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(volume_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
	add_hold_volume_events(volume_overlay);

	volume_label = lv_label_create(volume_overlay);
	lv_obj_set_style_text_font(volume_label, ui_scale_font_montserrat(28),
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(volume_label, lv_color_hex(0xE7EEFF), LV_PART_MAIN);
	lv_obj_center(volume_label);
	add_hold_volume_events(volume_label);

	is_playing = false;
	song_change_animating = false;
	queued_song_steps = 0;
	active_song_step = 0;
	volume_hold_active = false;
	volume_hold_last_y = 0;
	volume_swipe_step_px = ui_scale_px(8);
	if (volume_swipe_step_px < 1) {
		volume_swipe_step_px = 1;
	}
	last_volume_cmd_ms = 0;

	update_song_labels();
	update_media_controls_state();
	reset_song_progress();
	if (progress_timer != NULL) {
		lv_timer_delete(progress_timer);
		progress_timer = NULL;
	}
	progress_timer = lv_timer_create(progress_timer_cb, TIMER_PERIOD_MS, NULL);
	volume_hold_enable_at_ms = k_uptime_get() + VOLUME_HOLD_ENABLE_DELAY_MS;
}
