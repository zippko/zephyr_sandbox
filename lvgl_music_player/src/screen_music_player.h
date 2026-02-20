// SPDX-License-Identifier: Apache-2.0

#ifndef SCREEN_MUSIC_PLAYER_H_
#define SCREEN_MUSIC_PLAYER_H_

#include <stdbool.h>
#include <stdint.h>

typedef int (*ui_music_send_play_pause_cb_t)(bool play);
typedef int (*ui_music_send_usage_cb_t)(uint16_t usage);
typedef bool (*ui_music_bt_connected_cb_t)(void);

void ui_screen_music_player_init(ui_music_send_play_pause_cb_t play_pause_cb,
				 ui_music_send_usage_cb_t usage_cb,
				 ui_music_bt_connected_cb_t bt_connected_cb);
void ui_screen_music_player_show(void);

#endif /* SCREEN_MUSIC_PLAYER_H_ */
