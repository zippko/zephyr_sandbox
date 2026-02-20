// SPDX-License-Identifier: Apache-2.0

#ifndef SCREEN_MENU_H_
#define SCREEN_MENU_H_

#include <stdbool.h>
#include <stdint.h>
#include <lvgl.h>

#define UI_MENU_IDX_MUSIC 0
#define UI_MENU_IDX_BLUETOOTH 3

void ui_screen_menu_set_focus(uint8_t idx);
uint8_t ui_screen_menu_get_focus(void);
void ui_screen_menu_build(lv_obj_t *scr, bool clean_first);

#endif /* SCREEN_MENU_H_ */
