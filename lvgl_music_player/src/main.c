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

LOG_MODULE_REGISTER(lvgl_music_player, LOG_LEVEL_INF);

// Use the board's pwm-leds backlight node directly.
#define BACKLIGHT_NODE DT_NODELABEL(pwm_lcd0)

static const struct pwm_dt_spec backlight =
	PWM_DT_SPEC_GET(BACKLIGHT_NODE);

#define PROGRESS_MAX 100
#define TIMER_PERIOD_MS 1000
static lv_obj_t *progress_arc;
static lv_obj_t *elapsed_label;
static lv_obj_t *play_icon_label;
static lv_obj_t *title_label;
static lv_obj_t *artist_label;
static lv_obj_t *duration_label;
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
	ARG_UNUSED(e);

	is_playing = !is_playing;
	lv_label_set_text(play_icon_label,
			  is_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

static void next_song_event_cb(lv_event_t *e)
{
	ARG_UNUSED(e);

	current_song_index = (current_song_index + 1U) % ARRAY_SIZE(songs);
	update_song_labels();
	reset_song_progress();
}

static void prev_song_event_cb(lv_event_t *e)
{
	ARG_UNUSED(e);

	if (current_song_index == 0U) {
		current_song_index = ARRAY_SIZE(songs) - 1U;
	} else {
		current_song_index--;
	}

	update_song_labels();
	reset_song_progress();
}

static void create_music_player_screen(void)
{
	lv_obj_t *scr = lv_screen_active();

	lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
	lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
	lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, LV_PART_MAIN);
	lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);

	lv_obj_t *bg_img = lv_image_create(scr);
	lv_image_set_src(bg_img, &picture1_bg);
	lv_obj_set_size(bg_img, lv_pct(100), lv_pct(100));
	lv_image_set_inner_align(bg_img, LV_IMAGE_ALIGN_COVER);
	lv_obj_set_style_image_opa(bg_img, LV_OPA_50, LV_PART_MAIN);
	lv_obj_center(bg_img);

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
	lv_obj_remove_flag(progress_arc, LV_OBJ_FLAG_CLICKABLE);

	title_label = lv_label_create(scr);
	lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, LV_PART_MAIN);
	lv_obj_set_style_text_color(title_label, lv_color_hex(0xF0F4F8), LV_PART_MAIN);
	lv_obj_align(title_label, LV_ALIGN_CENTER, 0, -15);

	artist_label = lv_label_create(scr);
	lv_obj_set_style_text_font(artist_label, &lv_font_montserrat_14,
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(artist_label, lv_color_hex(0x9BB3C9),
				    LV_PART_MAIN);
	lv_obj_align_to(artist_label, title_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 1);
	play_icon_label = lv_label_create(scr);
	lv_label_set_text(play_icon_label, LV_SYMBOL_PLAY);
	lv_obj_set_style_text_font(play_icon_label, &lv_font_montserrat_28,
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(play_icon_label, lv_color_hex(0xE7EEFF),
				    LV_PART_MAIN);
	lv_obj_align(play_icon_label, LV_ALIGN_CENTER, 0, 37);
	lv_obj_add_flag(play_icon_label, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(play_icon_label, play_icon_event_cb, LV_EVENT_CLICKED,
			    NULL);

	lv_obj_t *next_icon_label = lv_label_create(scr);
	lv_label_set_text(next_icon_label, LV_SYMBOL_NEXT);
	lv_obj_set_style_text_font(next_icon_label, &lv_font_montserrat_28,
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(next_icon_label, lv_color_hex(0xE7EEFF),
				    LV_PART_MAIN);
	lv_obj_align_to(next_icon_label, play_icon_label, LV_ALIGN_OUT_RIGHT_MID, 24,
			0);
	lv_obj_add_flag(next_icon_label, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(next_icon_label, next_song_event_cb, LV_EVENT_CLICKED,
			    NULL);

	lv_obj_t *prev_icon_label = lv_label_create(scr);
	lv_label_set_text(prev_icon_label, LV_SYMBOL_PREV);
	lv_obj_set_style_text_font(prev_icon_label, &lv_font_montserrat_28,
				   LV_PART_MAIN);
	lv_obj_set_style_text_color(prev_icon_label, lv_color_hex(0xE7EEFF),
				    LV_PART_MAIN);
	lv_obj_align_to(prev_icon_label, play_icon_label, LV_ALIGN_OUT_LEFT_MID, -24,
			0);
	lv_obj_add_flag(prev_icon_label, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(prev_icon_label, prev_song_event_cb, LV_EVENT_CLICKED,
			    NULL);

	elapsed_label = lv_label_create(scr);
	lv_label_set_text(elapsed_label, "0:00");
	lv_obj_set_style_text_font(elapsed_label, &lv_font_montserrat_14,
			     LV_PART_MAIN);
	lv_obj_set_style_text_color(elapsed_label, lv_color_hex(0xDCE8F2),
			      LV_PART_MAIN);
	lv_obj_align(elapsed_label, LV_ALIGN_BOTTOM_MID, -22, -38);

	lv_obj_t *separator = lv_label_create(scr);
	lv_label_set_text(separator, "|");
	lv_obj_set_style_text_font(separator, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_style_text_color(separator, lv_color_hex(0xDCE8F2), LV_PART_MAIN);
	lv_obj_align(separator, LV_ALIGN_BOTTOM_MID, 0, -38);

	duration_label = lv_label_create(scr);
	lv_label_set_text(duration_label, "3:00");
	lv_obj_set_style_text_font(duration_label, &lv_font_montserrat_14,
			     LV_PART_MAIN);
	lv_obj_set_style_text_color(duration_label, lv_color_hex(0xDCE8F2),
			      LV_PART_MAIN);
	lv_obj_align(duration_label, LV_ALIGN_BOTTOM_MID, 22, -38);

	update_song_labels();
	reset_song_progress();
	lv_timer_create(progress_timer_cb, TIMER_PERIOD_MS, NULL);
}

int main(void)
{
	uint32_t backlight_period;

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
