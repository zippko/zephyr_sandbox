# lvgl_music_player

## Purpose
Render a touch-driven LVGL music player UI on the 1.28" round display with
play/pause, previous/next track selection, per-track duration, circular
progress visualization, and BLE HID Consumer Control output for Windows media
control.

## Architecture
- UI and player state are implemented in `src/main.c`.
- Background image is embedded as an LVGL image descriptor in `src/picture1_bg.c`.
- Zephyr display + LVGL integration provides rendering and touch input.
- Bluetooth LE HIDS service is initialized in `src/main.c` and advertises as
  a connectable peripheral.

## Environment
| Tool                 | Version |
| --------             | ------- |
| Zephyr               | 4.3.0 |
| Zephyr SDK           | zephyr-sdk-0.17.4 |
| LVGL                 | 9.5 |
| VS Code              | 1.109.2 |
| Workbench for Zephyr | 2.3.0 |

## Hardware
ESP32-S3 Development Board with 1.28" Round Touch LCD
(`esp32s3_touch_lcd_1_28/esp32s3/procpu`).

## Project Layout
- `src/main.c`: music-player screen, playback state machine, touch handlers.
- `src/picture1_bg.c`: embedded background image (240x240, RGB565).
- `prj.conf`: Zephyr/LVGL Kconfig options.
- `prj_ncs_hids.conf`: BLE/HIDS configuration for the media-control profile.
- `modules/nrf_ble_hids`: vendored Nordic HIDS implementation
  (`hids`, `gatt_pool`, `conn_ctx`).
- `boards/esp32s3_touch_lcd_1_28.overlay`: board overlay.

## Build and Flash
```sh
west build -b esp32s3_touch_lcd_1_28/esp32s3/procpu -p auto
west flash
```

## Run Behavior
- Displays the provided background image at 50% opacity.
- Shows song title and artist centered on the screen.
- Play icon toggles playback:
  - `Play` -> starts elapsed time and progress arc.
  - Sends BLE HID Consumer Control `Play` command (press + release report).
  - `Pause` -> stops progress updates.
  - Sends BLE HID Consumer Control `Pause` command (press + release report).
- Previous/Next icons change track from an internal song pool.
- Next icon also sends BLE HID Consumer Control `Scan Next Track`.
- Previous icon also sends BLE HID Consumer Control `Scan Previous Track`.
- Swipe gestures are rate-limited (350 ms) to avoid duplicate rapid media
  next/previous commands.
- Each song has a unique duration (2:00 to 3:00 range, second resolution).
- On track skip:
  - progress resets to `0:00`.
  - if current track was playing, next/previous starts playing immediately.
  - if paused, new track stays paused.

## Configuration Notes
- `CONFIG_LV_USE_IMAGE=y` is required for the background image widget.
- `CONFIG_INPUT_LOG_LEVEL_OFF=y` suppresses noisy non-fatal CST816S read logs.
- Nordic BLE HIDS dependencies are vendored in this repository under
  `modules/nrf_ble_hids` and enabled through `ZEPHYR_EXTRA_MODULES` in
  `CMakeLists.txt`.
- BLE/HIDS Kconfig settings are applied from `prj_ncs_hids.conf`.
- Bluetooth settings persistence uses NVS (`CONFIG_NVS=y`,
  `CONFIG_SETTINGS_NVS=y`) on the board `storage_partition`.
- Device appearance is set to Generic HID (`CONFIG_BT_DEVICE_APPEARANCE=960`)
  for Windows compatibility with consumer-control-only reports.
- Consumer Control commands are relative actions (play, pause, next, previous,
  volume up/down style events); OS absolute volume get/set is not provided by
  this HID profile.
- BLE security settings follow the Nordic
  `samples/bluetooth/peripheral_hids_keyboard` baseline:
  - `CONFIG_BT_SMP=y`
  - `CONFIG_BT_GATT_AUTO_SEC_REQ=n`
  - `CONFIG_BT_AUTO_PHY_UPDATE=n`
  - `CONFIG_BT_HIDS_DEFAULT_PERM_RW_ENCRYPT=y`
  - `CONFIG_BT_SETTINGS=y`

## Changelog-lite
- Added embedded background image pipeline and 50% transparent image layer.
- Added play/pause interaction and previous/next track controls.
- Added song pool with per-track durations and metadata switching.
- Synced progress arc and elapsed time to each selected track duration.
- Updated LVGL main-loop locking pattern to avoid UI stall conditions.
- Added vendored Nordic BLE HIDS module to make HIDS dependencies project-local.
- Added BLE HIDS service + advertising with Consumer Control report map.
- Added play/pause BLE media commands for Windows media control.
- Added next/previous BLE media commands on track skip actions.
- Added swipe gesture rate limit (350 ms) to reduce duplicate host media commands.
- Added Bluetooth security-oriented Kconfig aligned with Nordic
  `peripheral_hids_keyboard`.
