# lvgl_music_player

## Purpose
Render a touch-driven LVGL music player UI on the 1.28" round display with
play/pause, previous/next track selection, per-track duration, and circular
progress visualization.

## Architecture
- UI and player state are implemented in `src/main.c`.
- Background image is embedded as an LVGL image descriptor in `src/picture1_bg.c`.
- Zephyr display + LVGL integration provides rendering and touch input.

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
  - `Pause` -> stops progress updates.
- Previous/Next icons change track from an internal song pool.
- Each song has a unique duration (2:00 to 3:00 range, second resolution).
- On track skip:
  - progress resets to `0:00`.
  - if current track was playing, next/previous starts playing immediately.
  - if paused, new track stays paused.

## Configuration Notes
- `CONFIG_LV_USE_IMAGE=y` is required for the background image widget.
- `CONFIG_INPUT_LOG_LEVEL_OFF=y` suppresses noisy non-fatal CST816S read logs.

## Changelog-lite
- Added embedded background image pipeline and 50% transparent image layer.
- Added play/pause interaction and previous/next track controls.
- Added song pool with per-track durations and metadata switching.
- Synced progress arc and elapsed time to each selected track duration.
- Updated LVGL main-loop locking pattern to avoid UI stall conditions.
