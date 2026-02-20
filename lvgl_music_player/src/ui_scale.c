// SPDX-License-Identifier: Apache-2.0

#include "ui_scale.h"

#include <lvgl.h>
#include <stdbool.h>

static uint16_t ui_scale_permille = 1000U;

struct font_entry {
	uint8_t px;
	const lv_font_t *font;
};

static const struct font_entry montserrat_fonts[] = {
	{ 14, LV_FONT_DEFAULT },
#if defined(CONFIG_LV_FONT_MONTSERRAT_14) && CONFIG_LV_FONT_MONTSERRAT_14
	{ 14, &lv_font_montserrat_14 },
#endif
#if defined(CONFIG_LV_FONT_MONTSERRAT_16) && CONFIG_LV_FONT_MONTSERRAT_16
	{ 16, &lv_font_montserrat_16 },
#endif
#if defined(CONFIG_LV_FONT_MONTSERRAT_24) && CONFIG_LV_FONT_MONTSERRAT_24
	{ 24, &lv_font_montserrat_24 },
#endif
#if defined(CONFIG_LV_FONT_MONTSERRAT_28) && CONFIG_LV_FONT_MONTSERRAT_28
	{ 28, &lv_font_montserrat_28 },
#endif
#if defined(CONFIG_LV_FONT_MONTSERRAT_32) && CONFIG_LV_FONT_MONTSERRAT_32
	{ 32, &lv_font_montserrat_32 },
#endif
#if defined(CONFIG_LV_FONT_MONTSERRAT_40) && CONFIG_LV_FONT_MONTSERRAT_40
	{ 40, &lv_font_montserrat_40 },
#endif
#if defined(CONFIG_LV_FONT_MONTSERRAT_48) && CONFIG_LV_FONT_MONTSERRAT_48
	{ 48, &lv_font_montserrat_48 },
#endif
};

void ui_scale_refresh_for_active_screen(void)
{
	lv_obj_t *scr = lv_screen_active();
	int32_t w = 0;
	int32_t h = 0;
	int32_t w_scale;
	int32_t h_scale;
	int32_t min_scale;

	if (scr != NULL) {
		w = lv_obj_get_width(scr);
		h = lv_obj_get_height(scr);
	}

	if ((w <= 0) || (h <= 0)) {
		lv_display_t *disp = lv_display_get_default();

		if (disp != NULL) {
			w = lv_display_get_horizontal_resolution(disp);
			h = lv_display_get_vertical_resolution(disp);
		}
	}

	if ((w <= 0) || (h <= 0)) {
		ui_scale_permille = 1000U;
		return;
	}

	w_scale = (w * 1000) / UI_BASE_SIZE_PX;
	h_scale = (h * 1000) / UI_BASE_SIZE_PX;
	min_scale = (w_scale < h_scale) ? w_scale : h_scale;

	if (min_scale <= 0) {
		ui_scale_permille = 1000U;
		return;
	}

	ui_scale_permille = (uint16_t)min_scale;
}

int32_t ui_scale_px(int32_t value)
{
	int64_t scaled;
	int32_t abs_v = value;
	bool negative = false;

	if (value == 0) {
		return 0;
	}

	if (value < 0) {
		negative = true;
		abs_v = -value;
	}

	scaled = ((int64_t)abs_v * (int64_t)ui_scale_permille + 500LL) / 1000LL;
	if ((scaled == 0) && (abs_v > 0)) {
		scaled = 1;
	}

	return negative ? -(int32_t)scaled : (int32_t)scaled;
}

uint16_t ui_scale_permille_get(void)
{
	return ui_scale_permille;
}

const lv_font_t *ui_scale_font_montserrat(uint8_t base_px)
{
	size_t best_idx = 0U;
	uint16_t best_diff = UINT16_MAX;
	uint16_t target_px;

	if (base_px == 0U) {
		return LV_FONT_DEFAULT;
	}

	target_px = (uint16_t)(((uint32_t)base_px * ui_scale_permille + 500U) / 1000U);
	if (target_px == 0U) {
		target_px = 1U;
	}

	for (size_t i = 0; i < (sizeof(montserrat_fonts) / sizeof(montserrat_fonts[0]));
	     i++) {
		uint16_t font_px = montserrat_fonts[i].px;
		uint16_t diff = (font_px > target_px) ? (font_px - target_px) :
							(target_px - font_px);

		if (diff < best_diff) {
			best_diff = diff;
			best_idx = i;
		}
	}

	return montserrat_fonts[best_idx].font;
}
