// SPDX-License-Identifier: Apache-2.0

#ifndef UI_SCALE_H_
#define UI_SCALE_H_

#include <stdint.h>
#include <lvgl.h>

#define UI_BASE_SIZE_PX 240

void ui_scale_refresh_for_active_screen(void);
int32_t ui_scale_px(int32_t value);
uint16_t ui_scale_permille_get(void);
const lv_font_t *ui_scale_font_montserrat(uint8_t base_px);

#endif /* UI_SCALE_H_ */
