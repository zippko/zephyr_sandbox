## Zephyr Sandbox

Workspace for Zephyr-based embedded app experiments.

## Available Applications

### `dynamic_web`

Wi-Fi-enabled Zephyr web dashboard for `M5Stack Core2`.

Current high-level behavior:
- Connects to Wi-Fi using local credentials from `src/wifi_secrets.h`.
- Obtains an IPv4 address through DHCP.
- Runs an HTTP server and serves a Bootstrap 5.3 web UI.
- Exposes runtime status including `IP`, `SSID`, `uptime`, `CPU%`, and `RAM%`.
- Supports serving web content from firmware or from LittleFS storage.
- Supports MCUmgr filesystem updates for web assets.

Project files:
- App code: `dynamic_web/src/main.c`
- Services: `dynamic_web/src/wifi_service.c`, `dynamic_web/src/filesystem_service.c`, `dynamic_web/src/webserver_service.c`
- Web assets: `dynamic_web/web/`
- App notes: `dynamic_web/README.md`
- Build config: `dynamic_web/prj.conf`, `dynamic_web/Kconfig`, `dynamic_web/CMakeLists.txt`

### `lvgl_game_snake`

Touch-controlled Snake game for `ESP32-S3 Touch LCD 1.28`.

Current high-level behavior:
- Renders the game using `LVGL` on Zephyr.
- Uses touch input to steer the snake toward the touched point.
- Tracks score and resets after wall or self-collision.
- Spawns both normal and bonus food types.
- Can also run in the Zephyr native simulator with SDL touch input enabled.

Project files:
- App code: `lvgl_game_snake/src/main.c`
- App notes: `lvgl_game_snake/README.md`
- Board overlay: `lvgl_game_snake/boards/esp32s3_touch_lcd_1_28.overlay`
- Build config: `lvgl_game_snake/prj.conf`, `lvgl_game_snake/CMakeLists.txt`

### `lvgl_hello_world`

Minimal LVGL touch demo for `ESP32-S3 Touch LCD 1.28`.

Current high-level behavior:
- Shows a "Hello World" label with fade animation.
- Changes the label color on touch input.
- Demonstrates basic Zephyr + LVGL display and touch integration.
- Can also run in the Zephyr native simulator with SDL touch input enabled.

Project files:
- App code: `lvgl_hello_world/src/main.c`
- App notes: `lvgl_hello_world/README.md`
- Board overlay: `lvgl_hello_world/boards/esp32s3_touch_lcd_1_28.overlay`
- Build config: `lvgl_hello_world/prj.conf`, `lvgl_hello_world/CMakeLists.txt`

### `lvgl_music_player`

Touch-driven LVGL music player UI with BLE HID media control for `ESP32-S3 Touch LCD 1.28`.

Current high-level behavior:
- Displays a round-screen music player UI with album art background.
- Supports play/pause, previous/next track, progress arc, and simulated track metadata.
- Sends BLE HID Consumer Control commands for Windows media control.
- Supports long-press and vertical swipe volume gestures.
- Shows an on-screen passkey overlay during BLE pairing.
- Vendors the Nordic HIDS module locally for project-contained BLE HID support.

Project files:
- App code: `lvgl_music_player/src/main.c`
- UI sources: `lvgl_music_player/src/screen_*.c`, `lvgl_music_player/src/ui_*.c`
- BLE support: `lvgl_music_player/src/bluetooth_ctrl.c`, `lvgl_music_player/modules/nrf_ble_hids/`
- App notes: `lvgl_music_player/README.md`
- Build config: `lvgl_music_player/prj.conf`, `lvgl_music_player/prj_ncs_hids.conf`, `lvgl_music_player/CMakeLists.txt`

## App Map

| App | Focus | Hardware | Network | UI | Notes |
| --- | --- | --- | --- | --- | --- |
| `dynamic_web` | Embedded web server | `M5Stack Core2` | Wi-Fi | Bootstrap web UI | LittleFS + MCUmgr support |
| `lvgl_game_snake` | Casual game | `ESP32-S3 Touch LCD 1.28` | No | LVGL touchscreen | Native simulator support |
| `lvgl_hello_world` | LVGL demo | `ESP32-S3 Touch LCD 1.28` | No | LVGL touchscreen | Minimal animation/touch sample |
| `lvgl_music_player` | BLE media controller | `ESP32-S3 Touch LCD 1.28` | BLE | LVGL touchscreen | Windows media HID controls |

## Stack

- Zephyr RTOS
- Zephyr SDK `0.17.4`
- Primary UI library: `LVGL`
- Build system: `west` + `CMake`

Version notes:
- All current apps document Zephyr `4.3.0`.
- `lvgl_music_player` explicitly documents `LVGL 9.5`.
- `dynamic_web` targets `M5Stack Core2`, while the LVGL demo apps target the `ESP32-S3 Touch LCD 1.28` board family.

## Run

Build a project:

```sh
west build -b <board> -p auto
```

Flash a project:

```sh
west flash
```

Run commands from the specific project directory, for example:

```sh
cd dynamic_web
west build -b <board> -p auto
```

See each app-level `README.md` for exact board targets, overlays, and runtime notes.
