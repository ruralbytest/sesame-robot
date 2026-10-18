# Sesame head firmware

`sesame-head/` is the firmware for the round head board, a **Waveshare ESP32-S3-Touch-AMOLED-1.43C** (466 × 466 AMOLED). It is used with the XIAO ESP32-S3 body build of `sesame-firmware-main` (`XIAO_HEAD_LINK`, on by default). The body runs the robot and tells the head which face to show; the head draws it.

## What it does

- Draws the same 128 × 64 faces as the old OLED, from `firmware/face-bitmaps.h`, scaled ×3 to 384 × 192 in the middle of the screen.
- Plays each face's animation frames in `loop`, `once` or `boomerang` mode, at the same frame rates as the body.
- Listens for `F:<face>,<mode>` lines from the body, and from the Serial Monitor for testing. A repeat of the face already showing is ignored, because the body re-sends the current face every 3 seconds. Unknown names show the default face. Other lines, such as the body's boot log, are ignored.

Touch (sending `C:<command>` back to the body) and voice come in later phases.

## Build and flash

1. Copy `firmware/face-bitmaps.h` into `sesame-head/`. The copy is git-ignored; copy it again whenever the faces change. Without it the build stops with a message saying so.
2. Open `sesame-head/sesame-head.ino` in the Arduino IDE, with ESP32 board support by Espressif v3.x. No extra libraries are needed: the display uses ESP-IDF's `esp_lcd` and the SH8601/CO5300 panel driver in `src/`, the same as Waveshare's demo.
3. Tools settings, from Waveshare's demo:
   - Board: **ESP32S3 Dev Module**
   - USB CDC On Boot: **Enabled**
   - Flash Size: **8MB (64Mb)**
   - Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**
   - PSRAM: **OPI PSRAM**
4. Upload, then open the Serial Monitor at 115200 with "Newline". You should see `Sesame head ready.` and the default face.

With `arduino-cli`:

```
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=huge_app,PSRAM=opi,FlashMode=qio" firmware/head-firmware/sesame-head
```

## Testing without the body

Type these into the Serial Monitor:

```
F:happy
F:idle,boomerang
F:idle_blink,once
F:point,boomerang
```

## Wiring to the body

| Head pad       | XIAO pin | Notes                              |
| -------------- | -------- | ---------------------------------- |
| TXD (GPIO43)   | D7       | Head → body                        |
| RXD (GPIO44)   | D6       | Body → head                        |
| GND            | GND      | Also shared through the USB-C power pigtail |

The pads are inside the head's case, next to the USB-C port. Power the head through its USB-C port only; it has no 5 V pad.

## Settings

At the top of `sesame-head.ino`: `FACE_COLOR` (RGB565, white by default), `BRIGHTNESS_PERCENT` (80), and `LCD_X_GAP`. If the face looks shifted sideways on the real panel, adjust `LCD_X_GAP`; Waveshare's demo uses 8.

If you add a face with a non-default frame rate, add it to `faceFpsEntries[]` in both `sesame-head.ino` and `sesame-firmware-main.ino`.
