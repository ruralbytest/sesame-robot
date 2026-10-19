# Sesame Aurora — colourful companion head

Three original procedural characters for the Waveshare ESP32-S3-Touch-AMOLED-1.43C: Aurora the sunset lion, Biscuit the puppy, and Nova the neon robot. Native 466 × 466 RGB565 rendering, soft shape edges, expressive eyes, blinking, breathing, hearts, sparkles and a sound-reactive halo. Target refresh is 20 fps; actual speed appears in diagnostics.

## Try it

- Power the head over USB. A character appears without the body.
- Tap the screen: affection and a short synthesized pet call.
- Double-tap: the body does a trick, cycling wave → bow → dance → cute → point → shrug. Without a linked body (or with body control off) the head only shows that trick's expression and sound.
- Touch the screen while the body is moving: the body stops. That touch does nothing else.
- Swipe: cycle lion → puppy → robot.
- Hold still for about one second: calm face and purr; the body stops and any showcase ends.
- Short press BOOT while running: toggles an automatic expression showcase. Do not hold BOOT during power-on unless entering upload mode.
- Make a sound near the microphones: the halo responds; louder sounds can trigger a surprised reaction and chirp. A cooldown suppresses repeated responses to its own speaker. Surprise reactions are paused while the body moves, so servo noise does not trigger them; microphone reactions can also be disabled.
- Connect a phone to **Sesame-Aurora**, password **Sesame2026**. Open **http://192.168.4.1/**. This Wi-Fi network has no internet. Choose to remain connected if your phone asks.

The control page offers characters, moods, showcase, body-follow mode, speaker mute, microphone-reaction toggle and a **Touch moves body** switch (on at boot; turn it off for carrying or demos). Manual moods last 10 seconds. Local tap/sound reactions temporarily overlay the current body expression. Showcase runs until stopped. Settings reset on reboot, except **Battery fitted** and the **Wi-Fi auto-off** time, which are saved.

## Existing body link

This head listens at 115200 baud on RX GPIO44 for the body's `F:<name>,<loop|once|boomerang>` face lines and `M:<move>` move lines (`M:idle` when the body stops). On TX GPIO43 it sends `C:<trick>` for a double-tap and `C:stop` for a hold or a touch during a move, only while body control is on and the body is linked. It never sends walking commands, and all tricks it sends end on their own.

| Body XIAO | Head |
|---|---|
| D6 / TX / GPIO43 | RXD / GPIO44 |
| D7 / RX / GPIO44 | TXD / GPIO43 |
| GND | GND |

Each of the body's 19 moves has its own expression, badge label and sound: forward (WALK), backward (BACK), left, right, rest, stand (READY), wave (HELLO), dance, swim, point, pushup, bow, cute, freaky, worm, shake, shrug, dead and crab. A move stays on screen for at least 3 seconds. Moves come only from `M:` lines, so the body needs firmware that sends them; face names alone never show a move.

Faces that are not moves map to moods, each with its own sound (curious, the resting mood, is silent when it comes from the body): wave/walk/happy → happy; rest/sleepy/dead → sleepy; dance/excited/worm/freaky → party; cute/love/bow → love; angry/pushup/crab → brave; point/confused/thinking/shrug → thinking. Unknown names → curious. The old bitmap animation modes are accepted for protocol compatibility; this renderer animates continuous procedural expressions rather than replaying those bitmap frames. A small teal dot at the bottom means a valid body message arrived within eight seconds; violet means standalone.

## Battery

The face shows a phone-style battery icon under the character's name: the percentage, red at 15% or less, green with a bolt while charging. The voltage is read on GPIO4 (half the cell voltage). The charger's status pin doesn't reach the ESP32 usably on this board (GPIO7, used by Waveshare's example, always reads "not charging"), so charging is inferred: the bolt shows at once when USB is connected to a computer, and about a minute after plugging into a wall charger, once the voltage has kept rising. It goes away about a minute after unplugging a wall charger.

With no battery the reading is the charger's own voltage (about 4.17 V), which looks like a full battery. So the icon only shows when the control page's **Battery fitted** switch is on (`BATTERY:1` / `BATTERY:0` over USB). The setting is saved in flash and starts off. The percentage comes from a typical LiPo voltage curve, so it reads a little low while the speaker or Wi-Fi draws current.

The control page and the `?` status show the voltage, the lowest voltage since boot, the charging state, the uptime and why the head last started (`power on`, `brownout`, `crash`, `watchdog`, `USB reset`, ...). The brownout count is kept in flash across restarts. If the head keeps restarting on battery with `brownout` as the reason, the battery can't supply the current peaks: charge it, use a bigger cell, or lower the volume and brightness.

Buttons: **PWR** turns the board on (short press) and off (long press) when it runs on battery. **BOOT** toggles the showcase while running; held while pressing PWR or plugging in USB, it enters upload mode.

## Power savers

The speaker amplifier (PA, GPIO46) is switched off between sounds, which saves power and removes background hiss. It switches on with about 30 ms of silence before each sound, so the start isn't clipped, and switches off half a second after the sound ends. The `amp` field in `?` shows its state.

Wi-Fi is the head's biggest power draw. The control page's **Wi-Fi** section can turn it off after 1, 3 or 5 minutes with no phone connected (default: 1 minute, so connect your phone within a minute of starting the head). A connected phone keeps it on, and the countdown restarts when the last phone leaves. The setting is saved in flash. Restarting the head turns Wi-Fi back on; so does `WIFI:ON` over USB. `?` shows `wifi` (on/off), `wifiOff` (minutes, 0 = never) and `phones` (connected phones).

Not worth changing: re-sending the brightness every 2 s and reading the battery voltage without a battery each cost almost nothing.

## Audio and privacy

Speaker: ES8311 and PA GPIO46. Dual microphones: ES7210. Uses Waveshare's vendored Espressif codec drivers and official pins. Calls are synthesized, not recorded samples. Sound is analyzed locally for amplitude; there is no speech recognition, LLM integration, recording, audio upload, or cloud service. The web page receives only status and a loudness value. Muting disables audible pet calls; pausing microphone reactions disables their visual/behavioural effect, while the audio input and diagnostic meter remain active.

## Build and upload

Arduino ESP32 core 3.3.12, board **ESP32S3 Dev Module**, USB CDC On Boot **Enabled**, Flash **8MB**, partition **Huge APP**, PSRAM **OPI**, Flash Mode **QIO**. No external Arduino libraries are required. Audio drivers are included under `src/`.

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=huge_app,PSRAM=opi,FlashMode=qio" sesame-aurora
arduino-cli upload -p COM3 --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=huge_app,PSRAM=opi,FlashMode=qio" sesame-aurora
```

COM3 was the verified head port during development; identify the head again if ports change. Never upload this head sketch to the body.

The startup order retains the tested clear-before-display-on fix. The original one-time restart after cold power-on is retained, so allow roughly six seconds on first connection.

## Serial diagnostics (115200, newline)

- `?`: JSON status, measured fps, UART counters, microphone level, audio errors, speaker amplifier on/off (`amp`), touch count, `bodyControl`, `commandsSent`, battery (`batteryMv`, `batteryLowestMv`, `battery` %, `charging`), `startReason`, `brownouts` and `uptime` (seconds).
- `F:happy`: show a mood for 10 seconds without moving the body.
- `M:crab`: show a body move's expression and play its sound for 10 seconds without moving the body.
- `PET:0`, `PET:1`, `PET:2`: lion, puppy, robot.
- `BODY:1` / `BODY:0`: let touch move the body, or keep touch on the head only.
- `WIFIOFF:0|1|3|5`: Wi-Fi auto-off time in minutes (0 = never), saved. `WIFI:ON`: turn Wi-Fi back on after it switched off.
- `SHOW` / `FOLLOW`: showcase / body mode.
- `CHIRP`: play the greeting sound.
- `SNAP`: binary framebuffer export, ASCII `SNAP 434312\n` followed by 434312 little-endian RGB565 bytes. Use a binary reader, not a text monitor.

## Backup

The sibling `sesame-working-backup-2026-10-19` folder contains the working monochrome head source, face bitmap file and compiled binaries from before Aurora. Its restore script uploads only the head. The original repository head sketch is also left intact; Aurora is a separate sketch.

## Sources and licenses

Board pins and codec code: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.43C/tree/main/02_Example/arduino_v3.3.0/04_Audio_Test

Board documentation: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.43C

Vendored codec and panel drivers retain their Espressif Apache-2.0 headers. The panel draw function was adjusted to propagate pixel-transfer errors. Original artwork is drawn mathematically in `Scene.cpp`; no third-party character artwork is used.
