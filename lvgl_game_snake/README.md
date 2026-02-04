# lvgl_game_snake

## Purpose
Simple touch-controlled Snake game built with LVGL on Zephyr RTOS.

![Demo](assets/demo_lvlg_snake_game.gif)

## Environment
| Tool                 | Version |  
| --------             | ------- |
| Zephyr               | 4.3.0 |
| Zephyr SDK           | zephyr-sdk-0.17.4 |
| VS Code              | 1.108.2 |
| Workbench for Zephyr | 2.2.1 |

## Hardware
ESP32-S3 Development Board with 1.28" Round Touch LCD (Waveshare).

## Project Layout
- `src/main.c`: Snake game logic, rendering, and touch input handling.
- `prj.conf`: Zephyr and LVGL configuration.
- `boards/`: Board overlays or board-specific settings (if present).

## Build and Flash
From this directory, replace `<board>` with your target (example: `esp32s3_devkitc`).

```sh
west build -b <board> -p auto
west flash
```

## Run Behavior
The snake moves on a 20x20 grid and changes direction toward the touch point.
Touch anywhere on the screen to steer the snake. The game resets on wall or
self-collision. Score increments when the snake eats food.

Food types:
- Red food: normal fruit, grows the snake by 1 and adds +1 score.
- Yellow food (magic): spawns occasionally, grows the snake by +1 to +3 and
  adds the same bonus to the score.

## Configuration Notes
Touch input uses the Zephyr input subsystem and LVGL pointer device integration.

## Simulation
- Validated on Linux (Ubuntu 24.04.3 Desktop).
- Enable following options in `prj.conf`
    ```
    CONFIG_INPUT=y
    CONFIG_INPUT_SDL_TOUCH=y
    ```
- Build the sample for native simulator using `native_sim/native/64` as a build target.
    ```sh
    west build -b native_sim/native/64 -p auto
    ```
- Run zephyr.exe
    ```sh
    ./zephyr.exe
    ```  
- Screen pops up and reacts to user touch input, i.e. mouse click.

## Troubleshooting
- If the screen stays blank, confirm your display driver and board overlays.
- If touch does not work, verify the touch controller device tree bindings.
