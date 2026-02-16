// SPDX-License-Identifier: Apache-2.0

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <lvgl.h>
#include <lvgl_zephyr.h>
#include <stdbool.h>
#include <errno.h>

#if __has_include(<bluetooth/services/hids.h>)
#define HAS_NRF_HIDS 1
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <bluetooth/services/hids.h>
#else
#define HAS_NRF_HIDS 0
#endif

LOG_MODULE_REGISTER(lvgl_music_player, LOG_LEVEL_INF);

// Use the board's pwm-leds backlight node directly.
#define BACKLIGHT_NODE DT_NODELABEL(pwm_lcd0)

static const struct pwm_dt_spec backlight =
	PWM_DT_SPEC_GET(BACKLIGHT_NODE);

#define PROGRESS_MAX 100
#define TIMER_PERIOD_MS 1000
#define SONG_FADE_MS 160
#define VOLUME_SWIPE_STEP_PX 8
#define VOLUME_STEP_PERCENT 5U
#define VOLUME_HOLD_ENABLE_DELAY_MS 1200
#define GESTURE_RATE_LIMIT_MS 350
static bool bluetooth_ready;

#if HAS_NRF_HIDS
#define OUTPUT_REPORT_MAX_LEN 0
#define INPUT_REP_MEDIA_REF_ID 1
#define INPUT_REPORT_MEDIA_MAX_LEN 2
#define HID_CONSUMER_PLAY 0x00B0
#define HID_CONSUMER_PAUSE 0x00B1
#define HID_CONSUMER_SCAN_NEXT 0x00B5
#define HID_CONSUMER_SCAN_PREV 0x00B6

enum {
	INPUT_REP_MEDIA_IDX = 0
};

BT_HIDS_DEF(hids_obj, OUTPUT_REPORT_MAX_LEN, INPUT_REPORT_MEDIA_MAX_LEN);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE,
		      (CONFIG_BT_DEVICE_APPEARANCE >> 0) & 0xff,
		      (CONFIG_BT_DEVICE_APPEARANCE >> 8) & 0xff),
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

struct conn_mode {
	struct bt_conn *conn;
	bool reserved;
};

static struct conn_mode conn_mode[CONFIG_BT_HIDS_MAX_CLIENT_COUNT];

static lv_obj_t *progress_arc;
static lv_obj_t *elapsed_label;
static lv_obj_t *play_icon_label;
static lv_obj_t *title_label;
static lv_obj_t *artist_label;
static lv_obj_t *duration_label;
static lv_obj_t *volume_overlay;
static lv_obj_t *volume_label;
LV_IMAGE_DECLARE(picture1_bg);

struct song_info {
	const char *title;
	const char *artist;
	uint16_t duration_sec;
};

static const struct song_info songs[] = {
	{ "Midnight Tides", "Echo Harbor", 173U },
	{ "City Lights", "Neon Valley", 149U },
	{ "Drift Away", "The Shoreline", 132U },
	{ "Sundown Drive", "Velvet Highway", 164U },
	{ "Blue Horizon", "Skylane", 121U },
};

static uint32_t elapsed_sec;
static size_t current_song_index;
static bool is_playing;
static bool song_change_animating;
static int32_t queued_song_steps;
static int8_t active_song_step;
static uint8_t volume_percent = 70U;
static bool volume_hold_active;
static int16_t volume_hold_last_y;
static int64_t volume_hold_enable_at_ms;
static int64_t last_gesture_action_ms;

static int send_media_command(bool play);

static int advertising_start(void)
{
	int err;
	const struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
		BT_LE_ADV_OPT_CONN, BT_GAP_ADV_FAST_INT_MIN_2,
		BT_GAP_ADV_FAST_INT_MAX_2, NULL);

	err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err && err != -EALREADY) {
		LOG_ERR("Advertising failed (err %d)", err);
		return err;
	}

	LOG_INF("Advertising started");
	return 0;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		LOG_WRN("Connect failed to %s (0x%02x)", addr, err);
		return;
	}

	for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (!conn_mode[i].conn) {
			conn_mode[i].conn = conn;
			conn_mode[i].reserved = false;
			break;
		}
	}

	err = bt_hids_connected(&hids_obj, conn);
	if (err) {
		LOG_WRN("bt_hids_connected failed (err %d)", err);
	}

	LOG_INF("Connected %s", addr);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	int err;

	for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (conn_mode[i].conn == conn) {
			conn_mode[i].conn = NULL;
			conn_mode[i].reserved = false;
			break;
		}
	}

	err = bt_hids_disconnected(&hids_obj, conn);
	if (err) {
		LOG_WRN("bt_hids_disconnected failed (err %d)", err);
	}

	LOG_INF("Disconnected (reason 0x%02x)", reason);
	(void)advertising_start();
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	if (err) {
		LOG_WRN("Security failed for %s level %u err %d", addr, level, err);
	} else {
		LOG_INF("Security changed for %s level %u", addr, level);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void hids_pm_evt_handler(enum bt_hids_pm_evt evt,
				struct bt_conn *conn)
{
	ARG_UNUSED(evt);
	ARG_UNUSED(conn);
}

static void hid_init(void)
{
	int err;
	struct bt_hids_init_param hids_init_obj = { 0 };
	struct bt_hids_inp_rep *hids_inp_rep;

	static const uint8_t report_map[] = {
		/* Consumer Control (Play/Pause). */
		0x05, 0x0C,
		0x09, 0x01,
		0xA1, 0x01,
		0x85, INPUT_REP_MEDIA_REF_ID,
		0x15, 0x00,
		0x26, 0xFF, 0x03,
		0x19, 0x00,
		0x2A, 0xFF, 0x03,
		0x75, 0x10,
		0x95, 0x01,
		0x81, 0x00,
		0xC0
	};

	hids_init_obj.rep_map.data = report_map;
	hids_init_obj.rep_map.size = sizeof(report_map);
	hids_init_obj.info.bcd_hid = 0x0101;
	hids_init_obj.info.b_country_code = 0x00;
	hids_init_obj.info.flags = (BT_HIDS_REMOTE_WAKE |
				    BT_HIDS_NORMALLY_CONNECTABLE);

	hids_inp_rep =
		&hids_init_obj.inp_rep_group_init.reports[INPUT_REP_MEDIA_IDX];
	hids_inp_rep->size = INPUT_REPORT_MEDIA_MAX_LEN;
	hids_inp_rep->id = INPUT_REP_MEDIA_REF_ID;
	hids_init_obj.inp_rep_group_init.cnt++;

	hids_init_obj.is_kb = false;
	hids_init_obj.is_mouse = false;
	hids_init_obj.pm_evt_handler = hids_pm_evt_handler;

	err = bt_hids_init(&hids_obj, &hids_init_obj);
	if (err) {
		LOG_ERR("HIDS init failed (err %d)", err);
	} else {
		LOG_INF("HIDS initialized");
	}
}

static int send_consumer_usage(uint16_t usage)
{
	uint8_t report[INPUT_REPORT_MEDIA_MAX_LEN];
	bool has_conn = false;
	int err;

	report[0] = (uint8_t)(usage & 0xFFU);
	report[1] = (uint8_t)(usage >> 8);

	for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (!conn_mode[i].conn) {
			continue;
		}

		has_conn = true;
		err = bt_hids_inp_rep_send(&hids_obj, conn_mode[i].conn,
					   INPUT_REP_MEDIA_IDX, report,
					   sizeof(report), NULL);
		if (err) {
			return err;
		}
	}

	if (!has_conn) {
		return -ENOTCONN;
	}

	return 0;
}

static int send_media_command(bool play)
{
	uint16_t usage = play ? HID_CONSUMER_PLAY : HID_CONSUMER_PAUSE;
	int err;

	/* Press event. */
	err = send_consumer_usage(usage);
	if (err && err != -ENOTCONN) {
		return err;
	}

	/* Release event. */
	err = send_consumer_usage(0U);
	if (err && err != -ENOTCONN) {
		return err;
	}

	return 0;
}

static int send_media_usage_command(uint16_t usage)
{
	int err;

	err = send_consumer_usage(usage);
	if (err && err != -ENOTCONN) {
		return err;
	}

	err = send_consumer_usage(0U);
	if (err && err != -ENOTCONN) {
		return err;
	}

	return 0;
}

#else

static int send_media_command(bool play)
{
	ARG_UNUSED(play);
	return -ENOTSUP;
}

static int send_media_usage_command(uint16_t usage)
{
	ARG_UNUSED(usage);
	return -ENOTSUP;
}

#endif

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

	/* Re-apply alignment because label width changes with new text. */
	lv_obj_align(title_label, LV_ALIGN_CENTER, 0, -15);
	lv_obj_align_to(artist_label, title_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 1);
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

static void show_volume_overlay(void)
{
	lv_label_set_text_fmt(volume_label, "%u%%", volume_percent);
	lv_obj_clear_flag(volume_overlay, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(volume_overlay);
}

static void hide_volume_overlay(void)
{
	lv_obj_add_flag(volume_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void hold_volume_event_cb(lv_event_t *e)
{
	lv_indev_t *indev = lv_indev_active();
	lv_point_t p;
	int16_t dy;

	switch (lv_event_get_code(e)) {
	case LV_EVENT_LONG_PRESSED:
		if (k_uptime_get() < volume_hold_enable_at_ms) {
			break;
		}
		show_volume_overlay();
		volume_hold_active = true;
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

		while (dy <= -VOLUME_SWIPE_STEP_PX) {
			if (volume_percent < 100U) {
				volume_percent = MIN((uint8_t)(volume_percent +
							    VOLUME_STEP_PERCENT),
						    100U);
				show_volume_overlay();
			}
			volume_hold_last_y -= VOLUME_SWIPE_STEP_PX;
			dy += VOLUME_SWIPE_STEP_PX;
		}

		while (dy >= VOLUME_SWIPE_STEP_PX) {
			if (volume_percent > 0U) {
				if (volume_percent > VOLUME_STEP_PERCENT) {
					volume_percent -= VOLUME_STEP_PERCENT;
				} else {
					volume_percent = 0U;
				}
				show_volume_overlay();
			}
			volume_hold_last_y += VOLUME_SWIPE_STEP_PX;
			dy -= VOLUME_SWIPE_STEP_PX;
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
	lv_obj_add_event_cb(obj, hold_volume_event_cb, LV_EVENT_LONG_PRESSED,
			    NULL);
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

	is_playing = !is_playing;
	lv_label_set_text(play_icon_label,
			  is_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

	if (bluetooth_ready) {
		err = send_media_command(is_playing);
		if (err) {
			LOG_WRN("Failed to send HID media command '%s' (err %d)",
				is_playing ? "play" : "pause", err);
		}
	}
}

static void skip_to_next_song(void)
{
	int err;

	queued_song_steps++;
	start_song_change_animation();

	if (bluetooth_ready) {
		err = send_media_usage_command(HID_CONSUMER_SCAN_NEXT);
		if (err) {
			LOG_WRN("Failed to send HID media command 'next' (err %d)",
				err);
		}
	}
}

static void skip_to_prev_song(void)
{
	int err;

	queued_song_steps--;
	start_song_change_animation();

	if (bluetooth_ready) {
		err = send_media_usage_command(HID_CONSUMER_SCAN_PREV);
		if (err) {
			LOG_WRN("Failed to send HID media command 'previous' (err %d)",
				err);
		}
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

static void screen_gesture_event_cb(lv_event_t *e)
{
	lv_indev_t *indev = lv_indev_active();
	lv_dir_t gesture_dir;
	int64_t now_ms;

	ARG_UNUSED(e);

	if (indev == NULL) {
		return;
	}

	now_ms = k_uptime_get();
	if ((now_ms - last_gesture_action_ms) < GESTURE_RATE_LIMIT_MS) {
		return;
	}

	gesture_dir = lv_indev_get_gesture_dir(indev);
	if (gesture_dir == LV_DIR_LEFT) {
		last_gesture_action_ms = now_ms;
		skip_to_next_song();
	} else if (gesture_dir == LV_DIR_RIGHT) {
		last_gesture_action_ms = now_ms;
		skip_to_prev_song();
	}
}

static void create_music_player_screen(void)
{
	lv_obj_t *scr = lv_screen_active();

	lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
	lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
	lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, LV_PART_MAIN);
	lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
	lv_obj_add_event_cb(scr, screen_gesture_event_cb, LV_EVENT_GESTURE, NULL);
	add_hold_volume_events(scr);

	lv_obj_t *bg_img = lv_image_create(scr);
	lv_image_set_src(bg_img, &picture1_bg);
	lv_obj_set_size(bg_img, lv_pct(100), lv_pct(100));
	lv_image_set_inner_align(bg_img, LV_IMAGE_ALIGN_COVER);
	lv_obj_set_style_image_opa(bg_img, LV_OPA_50, LV_PART_MAIN);
	lv_obj_add_flag(bg_img, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_center(bg_img);
	add_hold_volume_events(bg_img);

	progress_arc = lv_arc_create(scr);
	lv_obj_set_size(progress_arc, 214, 214);
	lv_obj_center(progress_arc);
	lv_arc_set_range(progress_arc, 0, PROGRESS_MAX);
	lv_arc_set_bg_angles(progress_arc, 0, 360);
	lv_arc_set_rotation(progress_arc, 270);
	lv_arc_set_mode(progress_arc, LV_ARC_MODE_NORMAL);
	lv_arc_set_value(progress_arc, 0);
	lv_obj_set_style_arc_width(progress_arc, 8, LV_PART_MAIN);
	lv_obj_set_style_arc_opa(progress_arc, LV_OPA_TRANSP, LV_PART_MAIN);
	lv_obj_set_style_arc_width(progress_arc, 8, LV_PART_INDICATOR);
	lv_obj_set_style_arc_color(progress_arc, lv_color_hex(0x021E57),
			       LV_PART_INDICATOR);
	lv_obj_set_style_arc_rounded(progress_arc, true, LV_PART_INDICATOR);
	lv_obj_set_style_bg_opa(progress_arc, LV_OPA_TRANSP, LV_PART_KNOB);
	lv_obj_add_flag(progress_arc, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_remove_flag(progress_arc, LV_OBJ_FLAG_CLICKABLE);
	add_hold_volume_events(progress_arc);

	title_label = lv_label_create(scr);
	lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, LV_PART_MAIN);
	lv_obj_set_style_text_color(title_label, lv_color_hex(0xF0F4F8), LV_PART_MAIN);
	lv_obj_add_flag(title_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_align(title_label, LV_ALIGN_CENTER, 0, -15);
	add_hold_volume_events(title_label);

	artist_label = lv_label_create(scr);
	lv_obj_set_style_text_font(artist_label, &lv_font_montserrat_14,
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(artist_label, lv_color_hex(0x9BB3C9),
				    LV_PART_MAIN);
	lv_obj_add_flag(artist_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_align_to(artist_label, title_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 1);
	add_hold_volume_events(artist_label);
	play_icon_label = lv_label_create(scr);
	lv_label_set_text(play_icon_label, LV_SYMBOL_PLAY);
	lv_obj_set_style_text_font(play_icon_label, &lv_font_montserrat_28,
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(play_icon_label, lv_color_hex(0xE7EEFF),
				    LV_PART_MAIN);
	lv_obj_add_flag(play_icon_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_align(play_icon_label, LV_ALIGN_CENTER, 0, 37);
	lv_obj_add_flag(play_icon_label, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(play_icon_label, play_icon_event_cb, LV_EVENT_CLICKED,
			    NULL);
	add_hold_volume_events(play_icon_label);

	lv_obj_t *next_icon_label = lv_label_create(scr);
	lv_label_set_text(next_icon_label, LV_SYMBOL_NEXT);
	lv_obj_set_style_text_font(next_icon_label, &lv_font_montserrat_28,
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(next_icon_label, lv_color_hex(0xE7EEFF),
				    LV_PART_MAIN);
	lv_obj_add_flag(next_icon_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_align_to(next_icon_label, play_icon_label, LV_ALIGN_OUT_RIGHT_MID, 24,
			0);
	lv_obj_add_flag(next_icon_label, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(next_icon_label, next_song_event_cb, LV_EVENT_CLICKED,
			    NULL);
	add_hold_volume_events(next_icon_label);

	lv_obj_t *prev_icon_label = lv_label_create(scr);
	lv_label_set_text(prev_icon_label, LV_SYMBOL_PREV);
	lv_obj_set_style_text_font(prev_icon_label, &lv_font_montserrat_28,
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(prev_icon_label, lv_color_hex(0xE7EEFF),
				    LV_PART_MAIN);
	lv_obj_add_flag(prev_icon_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_align_to(prev_icon_label, play_icon_label, LV_ALIGN_OUT_LEFT_MID, -24,
			0);
	lv_obj_add_flag(prev_icon_label, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(prev_icon_label, prev_song_event_cb, LV_EVENT_CLICKED,
			    NULL);
	add_hold_volume_events(prev_icon_label);

	elapsed_label = lv_label_create(scr);
	lv_label_set_text(elapsed_label, "0:00");
	lv_obj_set_style_text_font(elapsed_label, &lv_font_montserrat_14,
			     LV_PART_MAIN);
	lv_obj_set_style_text_color(elapsed_label, lv_color_hex(0xDCE8F2),
			      LV_PART_MAIN);
	lv_obj_add_flag(elapsed_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_align(elapsed_label, LV_ALIGN_BOTTOM_MID, -22, -38);
	add_hold_volume_events(elapsed_label);

	lv_obj_t *separator = lv_label_create(scr);
	lv_label_set_text(separator, "|");
	lv_obj_set_style_text_font(separator, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_style_text_color(separator, lv_color_hex(0xDCE8F2), LV_PART_MAIN);
	lv_obj_add_flag(separator, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_align(separator, LV_ALIGN_BOTTOM_MID, 0, -38);
	add_hold_volume_events(separator);

	duration_label = lv_label_create(scr);
	lv_label_set_text(duration_label, "3:00");
	lv_obj_set_style_text_font(duration_label, &lv_font_montserrat_14,
			     LV_PART_MAIN);
	lv_obj_set_style_text_color(duration_label, lv_color_hex(0xDCE8F2),
			      LV_PART_MAIN);
	lv_obj_add_flag(duration_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_align(duration_label, LV_ALIGN_BOTTOM_MID, 22, -38);
	add_hold_volume_events(duration_label);

	volume_overlay = lv_obj_create(scr);
	lv_obj_set_size(volume_overlay, 88, 88);
	lv_obj_align(volume_overlay, LV_ALIGN_CENTER, 0, -4);
	lv_obj_set_style_radius(volume_overlay, LV_RADIUS_CIRCLE, LV_PART_MAIN);
	lv_obj_set_style_bg_color(volume_overlay, lv_color_hex(0x000000),
				  LV_PART_MAIN);
	lv_obj_set_style_bg_opa(volume_overlay, LV_OPA_70, LV_PART_MAIN);
	lv_obj_set_style_border_width(volume_overlay, 2, LV_PART_MAIN);
	lv_obj_set_style_border_color(volume_overlay, lv_color_hex(0xE7EEFF),
				      LV_PART_MAIN);
	lv_obj_set_style_pad_all(volume_overlay, 0, LV_PART_MAIN);
	lv_obj_remove_flag(volume_overlay, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(volume_overlay, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(volume_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
	add_hold_volume_events(volume_overlay);

	volume_label = lv_label_create(volume_overlay);
	lv_obj_set_style_text_font(volume_label, &lv_font_montserrat_16,
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(volume_label, lv_color_hex(0xE7EEFF),
				    LV_PART_MAIN);
	lv_obj_center(volume_label);
	add_hold_volume_events(volume_label);

	update_song_labels();
	reset_song_progress();
	lv_timer_create(progress_timer_cb, TIMER_PERIOD_MS, NULL);
	volume_hold_enable_at_ms = k_uptime_get() + VOLUME_HOLD_ENABLE_DELAY_MS;
}

int main(void)
{
	uint32_t backlight_period;
	int err;

	/* Turn on backlight if the board exposes it through pwm-leds. */
	if (!device_is_ready(backlight.dev)) {
		LOG_WRN("Backlight PWM device is not ready");
	} else {
		backlight_period = backlight.period;
		if (backlight_period == 0U) {
			backlight_period = PWM_USEC(4000);
		}

		if (pwm_set_dt(&backlight, backlight_period,
			       backlight_period / 2U) != 0) {
			LOG_WRN("Failed to enable backlight PWM");
		}
	}

	const struct device *display_dev =
		DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device is not ready");
		return 0;
	}

	/* Wait for display init */
	k_sleep(K_MSEC(200));	

	if (display_blanking_off(display_dev) != 0) {
		LOG_WRN("Could not unblank display");
	}

#if HAS_NRF_HIDS
	hid_init();

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
	} else {
		bluetooth_ready = true;
		LOG_INF("Bluetooth initialized");
		if (IS_ENABLED(CONFIG_SETTINGS)) {
			settings_load();
		}

		err = advertising_start();
		if (err) {
			LOG_WRN("Initial advertising start failed (err %d)", err);
		}
	}
#else
	ARG_UNUSED(err);
	LOG_WRN("BLE HIDS is unavailable in this Zephyr workspace (missing nRF HIDS module)");
#endif

	lvgl_lock();
	create_music_player_screen();
#ifndef CONFIG_LV_Z_RUN_LVGL_ON_WORKQUEUE
	lv_timer_handler();
#endif
	lvgl_unlock();

	LOG_INF("Round music player screen started");

	while (1) {
#ifndef CONFIG_LV_Z_RUN_LVGL_ON_WORKQUEUE
		uint32_t sleep_ms;

		lvgl_lock();
		sleep_ms = lv_timer_handler();
		lvgl_unlock();

		if (sleep_ms == LV_NO_TIMER_READY || sleep_ms > 100U) {
			sleep_ms = 10U;
		}

		k_sleep(K_MSEC(sleep_ms));
#else
		k_sleep(K_MSEC(10));
#endif
	}

	return 0;
}
