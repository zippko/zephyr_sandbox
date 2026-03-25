# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Use vendored Nordic BLE HIDS module from this repository.
list(APPEND ZEPHYR_EXTRA_MODULES
  "${CMAKE_CURRENT_SOURCE_DIR}/modules/nrf_ble_hids"
)
set(CONF_FILE "prj.conf;prj_ncs_hids.conf")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lvgl_music_player)

target_sources(app PRIVATE
  src/main.c
  src/bluetooth_ctrl.c
  src/ui_scale.c
  src/picture1_bg.c
  src/ui_screens.c
  src/screen_menu.c
  src/screen_bluetooth.c
  src/screen_music_player.c
)
