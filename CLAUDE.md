# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository overview

Sesame is an open-source ESP32 quadruped robot (8 servos, 2 per leg, plus a 128x64 SSD1306 OLED "face"). The only source code in this repo is the Arduino firmware in `firmware/`. Everything else is hardware (CAD, STLs, PCB Gerbers/BOMs in `hardware/`) and Markdown build/wiring docs (`docs/`, `motor-testing/`). The desktop tools (Sesame Studio, Companion App) live in separate repos, and `software/README.md` only links to them.

There is no test suite, linter, or CI. The way to check a change is to compile and flash it.

## Building / flashing

The firmware is an Arduino IDE sketch. Requirements (from `firmware/README.md`):
- ESP32 board support by Espressif (v2.0.0+)
- Libraries: **`ESP32Servo` pinned to v3.0.9** (newer versions have a bug where writing one servo affects other channels, madhephaestus/ESP32Servo#103), `Adafruit SSD1306`, `Adafruit GFX Library`
- Boards: Lolin S2 Mini → "LOLIN S2 Mini"; Distro Board V2/V3 → "ESP32S3 Dev Module"; Distro Board V1 → "ESP32 Dev Module". Use the "Default 4MB with spiffs" partition scheme, and set USB CDC On Boot to Enabled on S2/S3 boards.
- Arduino requires the sketch to sit in a folder with the same name as the `.ino`. To build, copy `sesame-firmware-main.ino` and all three `.h` files into a `sesame-firmware-main/` folder.

For a compile-only check with `arduino-cli` (the FQBN depends on the target board; the XIAO is `esp32:esp32:XIAO_ESP32S3`):
```
arduino-cli compile --fqbn esp32:esp32:lolin_s2_mini <path>/sesame-firmware-main
```

The Serial Monitor runs at 115200 baud.

## Board selection is done by commenting code

The target board is chosen in `sesame-firmware-main.ino`, not by a build flag. Near the top of the file there are commented-out blocks of `I2C_SDA`/`I2C_SCL` defines and `servoPins[8]` arrays for S2 Mini, Distro V1, V2 and V3. Exactly one block of each must be active. The checked-in default is **Lolin S2 Mini**. Don't change the active board unless asked. The exception is the **Seeed XIAO ESP32-S3**: it is auto-detected through `#if defined(ARDUINO_XIAO_ESP32S3)` (board "XIAO_ESP32S3"), and the commented blocks sit in its `#else`. Its own wiring checker is `firmware/debugging-firmware/xiao-mapping-tester/`. On the XIAO, `XIAO_HEAD_LINK` (default 1) drops the OLED, uses servoPins `{1..8}` and talks to a separate head board over UART1 on D6/D7 (`F:<face>,<mode>` and `M:<move>` out, `C:<command>` in, via `notifyHeadFace()`/`syncHeadMove()`/`pollHeadLink()`); `-DXIAO_HEAD_LINK=0` restores the XIAO + OLED build. `XIAO_REVERSE_BODY` (default 1; `-DXIAO_REVERSE_BODY=0` for the original orientation) swaps front and rear on the XIAO by reordering `servoPins` diagonally (R1↔L2, R2↔L1, R3↔L4, R4↔L3); pose code is unchanged. `HAS_OLED` guards every `display` use. The **PCBCupid Glyph ESP32-S3** is auto-detected the same way (`ARDUINO_PCBCUPID_GLYPH_S3` / `_PSRAM`, FQBN `esp32:esp32:Pcbcupid_GLYPH_S3`, sets `GLYPH_S3`): servoPins `{6,7,8,9,10,13,14,15}`, `GLYPH_HEAD_LINK` (default 1) runs the head link on UART1 IO17 (TX) / IO18 (RX), and `-DGLYPH_HEAD_LINK=0` uses the OLED on IO4/IO5. Keep IO1 (battery sense), IO21 (LED) and IO43/44 free. `-DUSE_PCA9685=1` (any board, default 0) swaps ESP32Servo for a PCA9685 at 0x40 on `I2C_SDA`/`I2C_SCL` (Adafruit PWM Servo Driver Library): servo index i → channel `pcaChannels[i]` (0–7), `servoPins` is not defined, and `setServoAngle()` maps 0–180 to 732–2500 µs (the same pulses ESP32Servo emits after clamping `attach(732, 2929)` to 2500 µs), capped at `PCA_SERVO_MAX_US` (2400 µs) because some servos ignore longer pulses. The pin tables are in `firmware/README.md` under "Hardware Abstraction Layer".

The same file also sets network mode at compile time with `ENABLE_NETWORK_MODE`, `NETWORK_SSID` and `NETWORK_PASS`. Keep these empty/false in commits. The AP SSID in the code is `Sesame-Controller`, although the docs say `Sesame-Controller-BETA`.

## Firmware architecture

Four files, all compiled as one translation unit through `#include`s in the `.ino`:

- **`sesame-firmware-main.ino`** holds all the global state, `setup()`/`loop()`, the WebServer routes, the Serial CLI, face rendering, idle/blink logic, WiFi provisioning, and `setServoAngle()`/`pressingCheck()`.
- **`movement-sequences.h`** holds every pose and gait as an `inline void runXxxPose()` function. It reaches into the main file through `extern` declarations (`setServoAngle`, `setFace*`, `delayWithFace`, `pressingCheck`, `currentCommand`, `frameDelay`, `walkCycles`). It also defines the `ServoName` enum (R1=0, R2=1, L1=2, L2=3, R4=4, R3=5, L3=6, L4=7; the index order is not the same as the name order) and `FaceAnimMode` (LOOP/ONCE/BOOMERANG).
- **`face-bitmaps.h`** holds the `FACE_LIST` X-macro and the raw image2cpp bitmap arrays.
- **`captive-portal.h`** holds the entire web UI as one `index_html[] PROGMEM` raw string literal (HTML, CSS and JS inline).

### Command dispatch model
Every input path (`/cmd?go=` / `/cmd?pose=`, `POST /api/command`, and the web UI) works by setting the global `String currentCommand`. `loop()` maps that string to a `runXxx()` function through an if/else chain. Animations block the loop, but they wait with `delayWithFace()` / `pressingCheck(cmd, ms)`, which keep calling `server.handleClient()`, the DNS server, and face updates. That keeps the robot responsive mid-animation. `pressingCheck` returns false, and the robot returns to stand, when `currentCommand` changes. That is how a held button gives continuous walking and a release stops it. Never use a bare `delay()` in movement code, or the web server will freeze.

`setServoAngle()` applies the per-servo `servoSubtrim[]` and then waits `motorCurrentDelay` ms. This staggered start is deliberate, to avoid brownouts.

The Serial CLI in `loop()` has its own separate if/else chain of short codes (`rn wf`, `rn dn`, `fc <face>`, `st <motor> <trim>`, `all <angle>`, `<motor> <angle>`, …).

### Adding a new pose/animation
Changes needed in several places:
1. Add a `runXxxPose()` prototype and an inline definition in `movement-sequences.h`.
2. Add a branch to the `currentCommand` dispatch in `loop()`, plus a Serial CLI short code if you want one.
3. Add a button to `captive-portal.h` (`pose('xxx')`).
4. Optionally add a matching face (see below) and an entry in `faceFpsEntries[]` in the `.ino` (and the same entry in `firmware/head-firmware/sesame-head/sesame-head.ino`, which keeps its own copy).

Pose code is usually generated by Sesame Studio and pasted in.

### Adding a face
Add `X(name)` to `FACE_LIST` and paste an `epd_bitmap_name[]` array (128x64, horizontal, from image2cpp) into `face-bitmaps.h`. Animation frames use the suffixes `epd_bitmap_name_1` … `_5`, up to 6 frames in total. Frames that don't exist are fine: each `epd_bitmap_*` is declared `__attribute__((weak))`, and a `#define const extern const` trick placed before the pasted arrays gives them external linkage, so a missing frame resolves to null and the code counts frames up to the first null. Keep that `#define` above all pasted bitmaps. The fallback face is spelled `defualt` (sic) in `FACE_LIST` and `face_defualt_frames`, and is exposed by name as `"default"`. Don't "fix" the spelling in only one place.

### Constraints worth knowing
- `/api/command` parses JSON by hand with `indexOf`; there is no JSON library. A body with a `face` and no `command` is treated as a face-only update.
- Timers 0–3 are reserved through `ESP32PWM::allocateTimer` for the servos. Adding more PWM peripherals can use up the timers and break WiFi or the captive portal.
- WiFi set through the web UI (`/api/wifi/scan|connect|status`) is a non-blocking state machine (`updateWifiSetup()` in `loop()`), and it is not saved to flash (`WiFi.persistent(false)`).
- `firmware/head-firmware/sesame-head/` is a separate sketch for the Waveshare 1.43C head board (`esp_lcd` + the vendored SH8601 driver in `src/`, no extra libraries). It builds against a git-ignored copy of `firmware/face-bitmaps.h` placed in its folder; see `firmware/head-firmware/README.md` for the board settings/FQBN.
- `firmware/debugging-firmware/sesame-motor-tester.ino` is a separate standalone sketch for testing servos one at a time (`<motor>,<angle>` and `stop` over serial). It does not share code with the main firmware.
