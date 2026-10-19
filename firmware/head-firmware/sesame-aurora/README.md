# Sesame Aurora — colourful companion head

Three original procedural characters for the Waveshare ESP32-S3-Touch-AMOLED-1.43C: Aurora the sunset lion, Biscuit the puppy, and Nova the neon robot. Native 466 × 466 RGB565 rendering, soft shape edges, expressive eyes, blinking, breathing, hearts, sparkles and a sound-reactive halo. Target refresh is 20 fps; actual speed appears in diagnostics.

## Try it

- Power the head over USB. A character appears without the body.
- Tap the screen: affection and a short synthesized pet call.
- Swipe: cycle lion → puppy → robot.
- Hold still for about one second: toggle an automatic expression showcase.
- Short press BOOT while running: also toggles showcase. Do not hold BOOT during power-on unless entering upload mode.
- Make a sound near the microphones: the halo responds; louder sounds can trigger a surprised reaction and chirp. A cooldown suppresses repeated responses to its own speaker. Servo noise can also trigger it; microphone reactions can be disabled.
- Connect a phone to **Sesame-Aurora**, password **Sesame2026**. Open **http://192.168.4.1/**. This Wi-Fi network has no internet. Choose to remain connected if your phone asks.

The control page offers characters, moods, showcase, body-follow mode, speaker mute and microphone-reaction toggle. Manual moods last 10 seconds. Local tap/sound reactions temporarily overlay the current body expression. Showcase runs until stopped. Settings reset on reboot.

## Existing body link

The body firmware is unchanged. This head listens for the existing `F:<name>,<loop|once|boomerang>` messages at 115200 baud on RX GPIO44. It does not transmit movement commands to the body.

| Body XIAO | Head |
|---|---|
| D6 / TX / GPIO43 | RXD / GPIO44 |
| D7 / RX / GPIO44 | TXD / GPIO43 |
| GND | GND |

Wave/walk/happy → happy; rest/sleepy/dead → sleepy; dance/excited/worm/freaky → party; cute/love/bow → love; angry/pushup/crab → brave; point/confused/thinking/shrug → thinking. Unknown names → curious. The old bitmap animation modes are accepted for protocol compatibility; this renderer animates continuous procedural expressions rather than replaying those bitmap frames. A small teal dot at the bottom means a valid body message arrived within eight seconds; violet means standalone.

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

- `?`: JSON status, measured fps, UART counters, microphone level, audio errors and touch count.
- `F:happy`: show a mood for 10 seconds without moving the body.
- `PET:0`, `PET:1`, `PET:2`: lion, puppy, robot.
- `SHOW` / `FOLLOW`: showcase / body mode.
- `CHIRP`: play the greeting sound.
- `SNAP`: binary framebuffer export, ASCII `SNAP 434312\n` followed by 434312 little-endian RGB565 bytes. Use a binary reader, not a text monitor.

## Backup

The sibling `sesame-working-backup-2026-10-19` folder contains the working monochrome head source, face bitmap file and compiled binaries from before Aurora. Its restore script uploads only the head. The original repository head sketch is also left intact; Aurora is a separate sketch.

## Sources and licenses

Board pins and codec code: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.43C/tree/main/02_Example/arduino_v3.3.0/04_Audio_Test

Board documentation: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.43C

Vendored codec and panel drivers retain their Espressif Apache-2.0 headers. The panel draw function was adjusted to propagate pixel-transfer errors. Original artwork is drawn mathematically in `Scene.cpp`; no third-party character artwork is used.
