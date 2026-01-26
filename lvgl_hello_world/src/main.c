
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>

// Use the board's pwm-leds backlight node directly.
#define BACKLIGHT_NODE DT_NODELABEL(pwm_lcd0)

static const struct pwm_dt_spec backlight =
	PWM_DT_SPEC_GET(BACKLIGHT_NODE);

static void screen_touch_cb(lv_event_t *e)
{
	lv_obj_t *label = lv_event_get_user_data(e);
	uint32_t rnd = k_cycle_get_32();
	lv_color_t color = lv_color_make((rnd >> 16) & 0xFF, (rnd >> 8) & 0xFF, rnd & 0xFF);

	lv_obj_set_style_text_color(label, color, 0);
}

static void label_set_opa(void *obj, int32_t value)
{
	lv_obj_set_style_text_opa(obj, (lv_opa_t)value, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_opa(obj, (lv_opa_t)value, LV_PART_MAIN | LV_STATE_PRESSED);
}

int main(void)
{
	const struct device *display_dev;
	lv_obj_t *label;
	uint32_t backlight_period;

	/* Turn on backlight */
	if (!device_is_ready(backlight.dev)) {
		return 0;
	}

	backlight_period = backlight.period;
	if (backlight_period == 0U) {
		backlight_period = PWM_USEC(4000);
	}

	pwm_set_dt(&backlight, backlight_period, backlight_period / 2U);

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		return 0;
	}

	/* Wait for display init */
	k_sleep(K_MSEC(200));

	label = lv_label_create(lv_screen_active());
	lv_label_set_text(label, "Hello World");
	lv_obj_center(label);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_28,
				   LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_28,
				   LV_PART_MAIN | LV_STATE_PRESSED);

	lv_obj_add_event_cb(lv_screen_active(), screen_touch_cb, LV_EVENT_PRESSED, label);

	/* Fade in/out animation for the label. */
	lv_anim_t anim;
	lv_anim_init(&anim);
	lv_anim_set_var(&anim, label);
	lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
	lv_anim_set_time(&anim, 1200);
	lv_anim_set_playback_time(&anim, 1200);
	lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
	lv_anim_set_exec_cb(&anim, label_set_opa);
	lv_anim_start(&anim);

	lv_timer_handler();
	display_blanking_off(display_dev);

	while (1) {
		lv_timer_handler();
		k_sleep(K_MSEC(10));
	}
}
