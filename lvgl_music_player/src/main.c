// SPDX-License-Identifier: Apache-2.0

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <lvgl.h>
#include <lvgl_zephyr.h>

#include "bluetooth_ctrl.h"
#include "screen_music_player.h"
#include "ui_screens.h"

LOG_MODULE_REGISTER(lvgl_music_player, LOG_LEVEL_INF);

/* Use the board's pwm-leds backlight node directly. */
#define BACKLIGHT_NODE DT_NODELABEL(pwm_lcd0)

#if DT_NODE_EXISTS(BACKLIGHT_NODE) && DT_NODE_HAS_STATUS(BACKLIGHT_NODE, okay)
#define HAS_BACKLIGHT_NODE 1
static const struct pwm_dt_spec backlight = PWM_DT_SPEC_GET(BACKLIGHT_NODE);
#else
#define HAS_BACKLIGHT_NODE 0
#endif

int main(void)
{
	uint32_t backlight_period;
	const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	int err;

	ui_screens_init(bt_ctrl_request_enabled, bt_ctrl_is_ready);
	ui_screen_music_player_init(bt_ctrl_send_play_pause, bt_ctrl_send_usage,
				    bt_ctrl_is_connected);
	bt_ctrl_init(ui_screens_show_pairing_passkey, ui_screens_hide_pairing_passkey);

#if HAS_BACKLIGHT_NODE
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
#else
	LOG_INF("No pwm_lcd0 backlight node in board definition");
#endif

	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device is not ready");
		return 0;
	}

	/* Wait for display init. */
	k_sleep(K_MSEC(200));

	if (display_blanking_off(display_dev) != 0) {
		LOG_WRN("Could not unblank display");
	}

	err = bt_ctrl_enable_stack_and_start();
	if (err) {
		LOG_WRN("Bluetooth controller init/start failed (err %d)", err);
	}

	lvgl_lock();
	ui_screens_show_default();
#ifndef CONFIG_LV_Z_RUN_LVGL_ON_WORKQUEUE
	lv_timer_handler();
#endif
	lvgl_unlock();

	LOG_INF("Default radial menu screen started");

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
