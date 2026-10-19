# Plan: spoken commands on the Aurora head

Status: **planned, not implemented** (written 2026-10-19). The touch-to-move work in `Aurora.cpp` is done; this plan builds on it.

## Goal

Say a phrase to the head and the body does a trick, for example "Hi ESP" … "wave". Recognition runs offline on the head with Espressif's ESP-SR library, which ships with the Arduino ESP32 core (3.3.12) as `libraries/ESP_SR`. No audio leaves the board.

## Decisions already made

| Question | Decision |
|---|---|
| Which sounds move the body | Spoken commands (ESP-SR) |
| Walking from voice | **No.** Only tricks that end on their own, the same rule as touch |
| Arming | **Off at every boot.** A "Voice moves body" switch on the web page, plus `VOICE:1` / `VOICE:0` over USB |
| Body changes | None. The head sends `C:<command>` / `C:stop`, which the body already handles (`handleHeadLine()` in `sesame-firmware-main.ino`) |

## What was checked on 2026-10-19

- **The head has 8 MB of flash** (`esptool flash-id`: ESP32-S3-PICO-1, 8 MB flash, 8 MB PSRAM). The core's speech partition scheme `esp_sr_16` needs 16 MB, so **we need our own partition table** (see below).
- The bundled models, `tools/esp32s3-libs/3.3.12/esp_sr/srmodels.bin`, are **3.34 MB**:
  - wake word `WN9_HIESP`, which means the wake word is fixed at **"Hi ESP"**;
  - English command model `MULTINET7_QUANT`.
  - A custom wake word such as "Hey Sesame" would need rebuilt ESP-IDF libraries or Espressif's paid wake word service. That's out of scope.
- Command phrases are plain English strings. The library turns them into phonemes at startup (`flite_g2p`), so the phrase list can be changed in code without retraining.
- The Arduino wrapper `ESP_SR.begin()` wants an `I2SClass`, but the head drives its codecs through `esp_codec_dev` on raw `i2s_tdm` handles (`Audio.cpp`). **Use the lower-level `sr_start(fill_cb, …)` from `esp32-hal-sr.h` instead.** It takes a callback that supplies audio, so the existing audio task stays in charge of I2S.
- A sketch-local `partitions.csv` overrides the board's partition menu (`platform.txt`, `recipe.hooks.prebuild.3`).
- ESP-SR starts three tasks: feed on core 0 (priority 5), and detect plus handler on **core 1** (priority 5 and max). Core 1 is where the Arduino `loop()` renders the face. **Expect the frame rate (about 13.7 fps today) to drop.** This is the main risk; Phase 0 measures it.

## Design

### Audio path
```
ES7210 mics ─I2S─► audioTask (Audio.cpp, core 0)
                    ├─ loudness → micLevel (unchanged: halo, mouth, surprise)
                    └─ copy each 16 ms stereo block ─► StreamBuffer (PSRAM, ~16 KB)
                                                        │
                       sr_start(fill_cb) ◄──────────────┘  fill_cb blocks on the buffer
                       ESP-SR feed / detect / handler tasks
                                │
                       sr event callback → queue → handled in interactions() (core 1, loop)
```
- Input format `"MM"` with `SR_CHANNELS_STEREO` (two mics, 16 kHz, 16 bit), which matches what the codec already delivers.
- Don't act on SR events inside the SR callback. Push `{event, command_id}` onto a small queue and handle it from `interactions()`, so all `Serial1` writes and `Scene` changes stay on the loop task.
- Optional, later: use the `"MMR"` format and feed the speaker samples we synthesise as the reference channel. This turns on echo cancellation, so the head can hear "stop" over its own sounds. Not needed for the first version.

### Flash layout (8 MB, new `partitions.csv` in the sketch folder)
```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x5000
app0,     app,  factory, 0x10000,  0x3C0000   # 3.75 MB app (today ~1.05 MB; ESP-SR adds roughly 1 MB)
model,    data, spiffs,  0x3D0000, 0x380000   # 3.5 MB for srmodels.bin (3.34 MB)
coredump, data, coredump,0x750000, 0x10000
```
- No OTA slot. The head is only ever flashed over USB.
- The model partition **must be named `model`**: `esp_srmodel_init("model")`.
- `arduino-cli upload` does not write `srmodels.bin` for a custom table. Flash it once, and again whenever the core version changes:
  `esptool --port COM3 write-flash 0x3D0000 <core>/tools/esp32s3-libs/3.3.12/esp_sr/srmodels.bin`
- Check the final offsets against the real app size from the Phase 0 build before relying on them.

### Commands (tricks only)

| Phrases (several per command are fine) | Sends |
|---|---|
| "stop", "freeze", "stand still" | `C:stop` |
| "wave", "say hello" | `C:wave` |
| "take a bow", "bow" | `C:bow` |
| "dance", "let's dance" | `C:dance` |
| "be cute" | `C:cute` |
| "point" | `C:point` |
| "shrug" | `C:shrug` |
| "lie down", "go to sleep" | `C:rest` |
| "stand up" | `C:stand` |

- All of these clear themselves on the body (`movement-sequences.h`), so no walking and no follow-up `stop` is needed. Never add `forward`, `backward`, `left`, `right`, `crab`, `worm` or `swim`.
- MultiNet supports up to 200 phrases. Short, distinct phrases recognise best; one-word phrases like "point" may be weak, so test and replace them if needed.

### Behaviour
1. **Waiting for "Hi ESP":** normal face.
2. **Wake word heard:** "listening" look (the THINKING mood, or perked eyes) plus a short rising chirp. Command mode lasts about 6 s (the library's timeout).
3. **Command recognised:**
   - Voice on and body linked (`canDriveBody()`-style check): send `C:<cmd>`. The body's `M:` reply brings that move's expression and sound, as with touch.
   - Voice off or body not linked: show the move's expression and sound on the head only (`previewMove` + `moveSound`), so voice can be tried safely.
4. **Command while the body is moving:**
   - "stop" works: it is the most useful voice command, so it must work over servo noise.
   - Other commands are ignored and the head shows a short "busy" face. (This differs from touch, where any touch stops; see the open questions.)
5. **Timeout with no command:** shrug face, back to waiting for the wake word.
6. **Not reacting to its own sounds:** while `soundUntil` is in the future, drop wake word events. Keep "stop" during body moves (see 4).
7. **The existing mic "surprised" reaction:** pause it while in command mode, so speaking a command doesn't make the head look startled.

### Switches and status
- Web page: a **"Voice moves body"** button, off at boot, next to "Touch moves body".
- USB: `VOICE:1` / `VOICE:0`.
- `?` status JSON gains:
  - `voice` (switch);
  - `sr` (library running: true/false);
  - `wakes`, `voiceCommands` (counters);
  - `lastVoice` (last recognised phrase).
- If ESP-SR fails to start (no model partition, out of memory), the head must still work exactly as it does today: log it, set `sr:false`, and carry on.

### Docs to update when implementing
- `README.md` "Audio and privacy" currently says there is **no speech recognition**. Change it to: offline phrase recognition on the head; no recording, upload or cloud.
- Control page footer, which has the same promise.
- `README.md` build section: the new partition table, the one-time `srmodels.bin` flash, and that `PartitionScheme=huge_app` in the FQBN is now overridden.
- The startup `Serial.println` help line.

## Phases

### Phase 0: spike (go / no-go), separate throwaway sketch
Put a test sketch in the scratchpad, not in the repo: the head's `Audio.cpp` codec init, the new partition table, `sr_start` with a fill callback, and 4 test phrases printed to Serial.

Measure:
- **App size** (does it fit 3.75 MB?) and **free internal RAM / PSRAM** after `sr_start`;
- **recognition in practice**: say each phrase 10 times at 30 cm and at 1 m, with and without the robot's servos running nearby;
- **false wakes** over about 10 min of normal room talk or TV;
- **CPU**: run a dummy 466×466 draw loop and compare fps with SR on and off.

**Go** if the commands are recognised reliably, false wakes are rare, and the head stays at **≥ 10 fps**. Otherwise, try one fix: lower the detect task load (`AFE_MODE_LOW_COST` is already used), or render at a lower rate while listening. If that doesn't work, fall back to the clap-pattern plan below.

### Phase 1: integrate
1. Add `partitions.csv` and flash `srmodels.bin`.
2. In `Audio.cpp`: the StreamBuffer tee in `audioTask`, `sr_start` after the codec opens, the event queue.
3. In `Aurora.cpp`: the `voiceControl` flag (off), handle the queued events in `interactions()`, the web/USB switch, the status fields.
4. Update the docs listed above.

### Phase 2: tune
- Mic gain (`esp_codec_dev_set_in_gain`, 24 dB today) for ESP-SR.
- Phrase wording based on the Phase 0 hit rates.
- Optionally, echo cancellation with the `"MMR"` format.

## How to test (on hardware)
1. Head on COM3 with the body unplugged: confirm the voice switch is off at boot, then check head-only reactions for every phrase.
2. Turn voice on, connect the body: every trick phrase makes the body move. "stop" during a dance stops it.
3. Ten minutes of TV or talking with the switch on: count false moves (target: zero; false wakes alone are acceptable).
4. Confirm touch still works (tap, double-tap, hold, swipe), and the fps in `?` stays at 10 or above.
5. Tell the user before every step that makes the body move.

## Open questions for implementation time
- While the body moves, should *any* recognised command act as stop (consistent with touch), or only "stop" (current plan)?
- Should ESP-SR run at all while the voice switch is off? Running it allows head-only voice reactions; stopping it saves CPU and frame rate.
- Is "Hi ESP" acceptable as the wake word, or is a custom wake word worth the extra effort later?

## Fallback if voice doesn't fit
Clap patterns using the loudness meter that already exists: 2 claps → next trick, 3 claps → dance, single clap → head only. No new library, no partition change, and the same arming switch.
