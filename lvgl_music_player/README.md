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
- Shows song title and artist (simulated, not using real metadata from host) centered on the screen.
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
- Long-press + vertical swipe sends BLE HID Consumer Control `Volume Up` /
  `Volume Down` commands to the host.
- Volume overlay appears immediately on long press, then shows `+` / `-`
  while swipe steps are generating volume commands.
- Volume overlay shows only `+` or `-` (no absolute percentage value).
- Volume command sends are rate-limited (140 ms interval) to keep host volume
  changes smooth and avoid command bursts.
- During BLE pairing, passkey is shown in a centered on-screen overlay and is
  hidden when pairing completes, fails, is canceled, or disconnects.
- Each song has a unique duration (2:00 to 3:00 range, second resolution). All simulated (not using real metadata from host).
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
- BLE auth callbacks are registered (`bt_conn_auth_cb_register`,
  `bt_conn_auth_info_cb_register`) and numeric comparison passkey confirmation
  is handled in firmware.
- DIS is enabled with PnP ID metadata (`CONFIG_BT_DIS=y`) for host identity
  information while keeping the profile focused on HIDS consumer control.
- On security errors due stale host/device bond keys (for example
  `PIN_OR_KEY_MISSING`), remove the device on Windows and re-pair.
- Pairing recovery is host-driven (forget/re-pair on Windows). Automatic local
  bond clearing is not used.
- For stability on ESP32-S3, stack sizes were increased in
  `prj_ncs_hids.conf`:
  - `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=4096`
  - `CONFIG_BT_RX_STACK_SIZE=4096`
- LVGL draw buffer alignment warnings were addressed by:
  - `CONFIG_LV_ATTRIBUTE_MEM_ALIGN_SIZE=4` in `prj.conf`
  - explicit image data alignment in `src/picture1_bg.c`

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
- Added BLE auth callback flow and on-screen passkey overlay for pairing UX.
- Updated volume UX: larger `+`/`-` indicator and immediate overlay on long press.
- Removed stale auto-bond-clear documentation and aligned notes to current code.
