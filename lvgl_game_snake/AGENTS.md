# AGENTS.md

## How to use this file
* Skim once at the start of a session to align on global policies.
* Ask the user when instructions conflict or feel unsafe.

## Models
* Prefer `gpt-5.2-codex`. If unavailable, state active model and limitations.

## Agent conduct
* Verify assumptions before executing commands; call out uncertainties first.
* Ask for clarification when the request is ambiguous, destructive, or risky.
* Summarize intent before performing multi-step fixes so the user can redirect early.
* Cite the source when using documentation; quote exact lines instead of paraphrasing from memory.
* Break work into incremental steps and confirm each step with the smallest relevant check before moving on.

## Project Overview
This is Zephyr RTOS application.

## Environment
- Zephyr: 4.3.0
- Zephyr SDK: 0.17.4
- LVGL: 9.5
- Board: esp32s3_touch_lcd_1_28/esp32s3/procpu

## Project Structure

## Code Style
Use Zephyr RTOS project code style.

## State & living docs
Maintain:
* `README.md` — stable overview.

Refresh triggers: contradictions, omissions, flaky tests, or version uncertainty.

Refresh includes:
* `README.md`: purpose, architecture, stack with versions, run instructions, changelog-lite.