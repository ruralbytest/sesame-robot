// sesame-head.ino - face firmware for the Sesame head board.
//
// Board: Waveshare ESP32-S3-Touch-AMOLED-1.43C (466 x 466 round AMOLED,
// CO5300 panel over QSPI). The robot's body (a XIAO ESP32-S3 running
// sesame-firmware-main with XIAO_HEAD_LINK) sends it one text line per face
// change over UART; this sketch draws and animates the face.
//
//   Body -> head:  F:<face>,<mode>    mode = loop | once | boomerang (optional, default loop)
//
// The faces are the same 128 x 64 1-bit bitmaps as the old OLED, from
// firmware/face-bitmaps.h, scaled x3 to 384 x 192 in the middle of the screen.
// Lines can also be typed into the Serial Monitor (115200, newline) to test
// without a body, for example "F:happy" or "F:idle,boomerang".
//
// Before building, copy firmware/face-bitmaps.h into this folder.
// Arduino IDE settings (from Waveshare's demo):
//   Board "ESP32S3 Dev Module", USB CDC On Boot "Enabled", Flash Size "8MB",
//   Partition Scheme "Huge APP (3MB No OTA/1MB SPIFFS)", PSRAM "OPI PSRAM".

#include <Arduino.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include "src/esp_lcd_sh8601.h"

#if __has_include("face-bitmaps.h")
#include "face-bitmaps.h"
#else
#error "Copy firmware/face-bitmaps.h into this sketch folder before building (see the README)."
#endif

// ====== PINS (Waveshare demo bsp_config.h) ======
#define LCD_HOST    SPI2_HOST
#define LCD_SCK     14
#define LCD_D0      9
#define LCD_D1      10
#define LCD_D2      11
#define LCD_D3      12
#define LCD_CS      15
#define LCD_RST     13
#define LCD_SIZE    466
#define LCD_X_GAP   8     // the panel's visible columns start at 8

// Link to the body. The head's TXD/RXD test pads are UART0's pins; with USB
// CDC On Boot enabled, Serial is USB, so UART1 is mapped onto them.
#define LINK_RX     44    // RXD pad, from the XIAO's D6
#define LINK_TX     43    // TXD pad, to the XIAO's D7
#define LINK_BAUD   115200

// ====== LOOK ======
// RGB565. AMOLED black pixels are off, so the background costs no power.
#define FACE_COLOR  0xFFFF
#define BG_COLOR    0x0000
#define BRIGHTNESS_PERCENT 80

// The panel only accepts even start coordinates and even sizes.
static const int BMP_W = 128, BMP_H = 64, SCALE = 3;
static const int FACE_W = BMP_W * SCALE;               // 384
static const int FACE_H = BMP_H * SCALE;               // 192
static const int FACE_X = 40;                          // (466 - 384) / 2 = 41, rounded to even
static const int FACE_Y = 136;                         // (466 - 192) / 2 = 137, rounded to even
static const int STRIP_BMP_ROWS = 16;                  // bitmap rows per transfer (48 screen rows)
static const int STRIP_PIXELS = FACE_W * STRIP_BMP_ROWS * SCALE;
static const int CLEAR_ROWS = (STRIP_PIXELS / LCD_SIZE) & ~1;  // full-width rows per clear transfer

// ====== FACE TABLE (same layout as the body firmware) ======
enum FaceAnimMode : uint8_t { FACE_ANIM_LOOP = 0, FACE_ANIM_ONCE = 1, FACE_ANIM_BOOMERANG = 2 };

struct FaceEntry {
  const char* name;
  const unsigned char* const* frames;
  uint8_t maxFrames;
};

static const uint8_t MAX_FACE_FRAMES = 6;

#define MAKE_FACE_FRAMES(name) \
  const unsigned char* const face_##name##_frames[] = { \
    epd_bitmap_##name, epd_bitmap_##name##_1, epd_bitmap_##name##_2, \
    epd_bitmap_##name##_3, epd_bitmap_##name##_4, epd_bitmap_##name##_5 \
  };

#define X(name) MAKE_FACE_FRAMES(name)
FACE_LIST
#undef X
#undef MAKE_FACE_FRAMES

const FaceEntry faceEntries[] = {
#define X(name) { #name, face_##name##_frames, MAX_FACE_FRAMES },
  FACE_LIST
#undef X
  { "default", face_defualt_frames, MAX_FACE_FRAMES }
};

// Frame rates. Keep in sync with faceFpsEntries[] in sesame-firmware-main.ino,
// so ONCE animations (like the idle blink) end when the body expects.
struct FaceFpsEntry { const char* name; uint8_t fps; };
const FaceFpsEntry faceFpsEntries[] = {
  { "walk", 1 }, { "rest", 1 }, { "swim", 1 }, { "dance", 1 }, { "wave", 1 },
  { "point", 5 }, { "stand", 1 }, { "cute", 1 }, { "pushup", 1 }, { "freaky", 1 },
  { "bow", 1 }, { "worm", 1 }, { "shake", 1 }, { "shrug", 1 }, { "dead", 2 },
  { "crab", 1 }, { "idle", 1 }, { "idle_blink", 7 }, { "default", 1 },
  { "happy", 1 }, { "talk_happy", 1 }, { "sad", 1 }, { "talk_sad", 1 },
  { "angry", 1 }, { "talk_angry", 1 }, { "surprised", 1 }, { "talk_surprised", 1 },
  { "sleepy", 1 }, { "talk_sleepy", 1 }, { "love", 1 }, { "talk_love", 1 },
  { "excited", 1 }, { "talk_excited", 1 }, { "confused", 1 }, { "talk_confused", 1 },
  { "thinking", 1 }, { "talk_thinking", 1 },
};
static const int DEFAULT_FACE_FPS = 8;

// ====== STATE ======
esp_lcd_panel_io_handle_t panelIo = nullptr;
esp_lcd_panel_handle_t panel = nullptr;
SemaphoreHandle_t flushDone = nullptr;
uint16_t* stripBuf = nullptr;     // DMA-capable, one strip of pixels

String requestedFace = "";        // exactly what the last F: line asked for
FaceAnimMode requestedMode = FACE_ANIM_LOOP;
const unsigned char* const* faceFrames = nullptr;
uint8_t faceFrameCount = 0;
uint8_t faceFrameIndex = 0;
int8_t faceFrameDirection = 1;
bool faceAnimFinished = false;
FaceAnimMode faceMode = FACE_ANIM_LOOP;
int faceFps = DEFAULT_FACE_FPS;
unsigned long lastFrameMs = 0;

// ====== DISPLAY ======
// Init sequence from Waveshare's demo for this panel.
static const sh8601_lcd_init_cmd_t lcdInitCmds[] = {
  { 0xFE, (uint8_t[]){ 0x00 }, 1, 0 },
  { 0xC4, (uint8_t[]){ 0x80 }, 1, 0 },
  { 0x3A, (uint8_t[]){ 0x55 }, 1, 0 },   // RGB565
  { 0x35, (uint8_t[]){ 0x00 }, 1, 0 },
  { 0x53, (uint8_t[]){ 0x20 }, 1, 0 },
  { 0x51, (uint8_t[]){ 0xFF }, 1, 0 },
  { 0x36, (uint8_t[]){ 0xC0 }, 1, 0 },   // MADCTL: rotate 180 so USB-C is at the bottom
  { 0x63, (uint8_t[]){ 0xFF }, 1, 0 },
  { 0x2A, (uint8_t[]){ 0x00, 0x06, 0x01, 0xD7 }, 4, 0 },
  { 0x2B, (uint8_t[]){ 0x00, 0x00, 0x01, 0xD1 }, 4, 0 },
  { 0x11, (uint8_t[]){ 0x00 }, 0, 100 }, // sleep out
  { 0x29, (uint8_t[]){ 0x00 }, 0, 0 },   // display on
};

static bool IRAM_ATTR onColorTransDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(flushDone, &woken);
  return woken == pdTRUE;
}

// The pixel transfer runs in the background; wait before reusing the buffer.
void pushPixels(int x, int y, int w, int h) {
  esp_lcd_panel_draw_bitmap(panel, x, y, x + w, y + h, stripBuf);
  xSemaphoreTake(flushDone, portMAX_DELAY);
}

// The panel takes RGB565 high byte first.
static inline uint16_t panelColor(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

void setBrightness(int percent) {
  uint8_t level = (uint8_t)(constrain(percent, 0, 100) * 255 / 100);
  esp_lcd_panel_io_tx_param(panelIo, (0x02 << 24) | (0x51 << 8), &level, 1);
}

void clearScreen() {
  uint16_t bg = panelColor(BG_COLOR);
  for (int i = 0; i < LCD_SIZE * CLEAR_ROWS; i++) stripBuf[i] = bg;
  for (int y = 0; y < LCD_SIZE; y += CLEAR_ROWS) {
    pushPixels(0, y, LCD_SIZE, min(CLEAR_ROWS, LCD_SIZE - y));
  }
}

// Draw one 128 x 64 image2cpp bitmap (MSB first, 16 bytes per row), each
// pixel as a SCALE x SCALE block.
void drawFaceBitmap(const unsigned char* bitmap) {
  if (bitmap == nullptr) return;
  const uint16_t fg = panelColor(FACE_COLOR), bg = panelColor(BG_COLOR);
  for (int by = 0; by < BMP_H; by += STRIP_BMP_ROWS) {
    uint16_t* out = stripBuf;
    for (int r = by; r < by + STRIP_BMP_ROWS; r++) {
      const unsigned char* row = bitmap + r * (BMP_W / 8);
      uint16_t* line = out;
      for (int bx = 0; bx < BMP_W; bx++) {
        uint16_t c = (pgm_read_byte(row + (bx >> 3)) & (0x80 >> (bx & 7))) ? fg : bg;
        *out++ = c; *out++ = c; *out++ = c;
      }
      for (int k = 1; k < SCALE; k++) {
        memcpy(out, line, FACE_W * sizeof(uint16_t));
        out += FACE_W;
      }
    }
    pushPixels(FACE_X, FACE_Y + by * SCALE, FACE_W, STRIP_BMP_ROWS * SCALE);
  }
}

void initDisplay() {
  flushDone = xSemaphoreCreateBinary();
  stripBuf = (uint16_t*)heap_caps_malloc(STRIP_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA);
  if (stripBuf == nullptr) {
    Serial.println("Display buffer allocation failed");
    while (true) delay(1000);
  }

  spi_bus_config_t bus = {};
  bus.sclk_io_num = LCD_SCK;
  bus.data0_io_num = LCD_D0;
  bus.data1_io_num = LCD_D1;
  bus.data2_io_num = LCD_D2;
  bus.data3_io_num = LCD_D3;
  bus.max_transfer_sz = STRIP_PIXELS * sizeof(uint16_t);
  ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t io = {};
  io.cs_gpio_num = LCD_CS;
  io.dc_gpio_num = -1;
  io.spi_mode = 0;
  io.pclk_hz = 40 * 1000 * 1000;
  io.trans_queue_depth = 4;
  io.on_color_trans_done = onColorTransDone;
  io.lcd_cmd_bits = 32;
  io.lcd_param_bits = 8;
  io.flags.quad_mode = true;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io, &panelIo));

  sh8601_vendor_config_t vendor = {};
  vendor.init_cmds = lcdInitCmds;
  vendor.init_cmds_size = sizeof(lcdInitCmds) / sizeof(lcdInitCmds[0]);
  vendor.flags.use_qspi_interface = 1;

  esp_lcd_panel_dev_config_t dev = {};
  dev.reset_gpio_num = LCD_RST;
  dev.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  dev.bits_per_pixel = 16;
  dev.vendor_config = &vendor;
  ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(panelIo, &dev, &panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, LCD_X_GAP, 0));

  clearScreen();
  setBrightness(BRIGHTNESS_PERCENT);
}

// ====== FACES ======
uint8_t countFrames(const unsigned char* const* frames, uint8_t maxFrames) {
  uint8_t count = 0;
  while (frames != nullptr && count < maxFrames && frames[count] != nullptr) count++;
  return count;
}

int fpsForFace(const String& name) {
  for (const FaceFpsEntry& e : faceFpsEntries) {
    if (name.equalsIgnoreCase(e.name)) return e.fps;
  }
  return DEFAULT_FACE_FPS;
}

// Unknown names fall back to "default", as on the body.
void showFace(const String& name, FaceAnimMode mode) {
  String shown = "default";
  faceFrames = face_defualt_frames;
  for (const FaceEntry& e : faceEntries) {
    if (name.equalsIgnoreCase(e.name) && countFrames(e.frames, e.maxFrames) > 0) {
      faceFrames = e.frames;
      shown = e.name;
      break;
    }
  }
  faceFrameCount = countFrames(faceFrames, MAX_FACE_FRAMES);
  faceFrameIndex = 0;
  faceFrameDirection = 1;
  faceAnimFinished = false;
  faceMode = mode;
  faceFps = max(1, fpsForFace(shown));
  lastFrameMs = millis();
  if (faceFrameCount > 0) drawFaceBitmap(faceFrames[0]);
}

void updateAnimation() {
  if (faceFrameCount <= 1) return;
  if (faceMode == FACE_ANIM_ONCE && faceAnimFinished) return;
  unsigned long now = millis();
  if (now - lastFrameMs < 1000UL / faceFps) return;
  lastFrameMs = now;

  if (faceMode == FACE_ANIM_LOOP) {
    faceFrameIndex = (faceFrameIndex + 1) % faceFrameCount;
  } else if (faceMode == FACE_ANIM_ONCE) {
    if (faceFrameIndex + 1 >= faceFrameCount) {
      faceAnimFinished = true;
      return;  // already showing the last frame
    }
    faceFrameIndex++;
  } else {  // boomerang
    if (faceFrameDirection > 0 && faceFrameIndex + 1 >= faceFrameCount) faceFrameDirection = -1;
    else if (faceFrameDirection < 0 && faceFrameIndex == 0) faceFrameDirection = 1;
    faceFrameIndex += faceFrameDirection;
  }
  drawFaceBitmap(faceFrames[faceFrameIndex]);
}

// ====== MESSAGES ======
bool parseMode(const String& s, FaceAnimMode& mode) {
  if (s.length() == 0 || s.equalsIgnoreCase("loop")) { mode = FACE_ANIM_LOOP; return true; }
  if (s.equalsIgnoreCase("once")) { mode = FACE_ANIM_ONCE; return true; }
  if (s.equalsIgnoreCase("boomerang")) { mode = FACE_ANIM_BOOMERANG; return true; }
  return false;
}

// Anything that isn't an F: line is ignored: the body's boot log arrives on
// the same wire at every reset.
void handleLine(const char* line, bool fromUsb) {
  if (strncmp(line, "F:", 2) != 0) {
    if (fromUsb) Serial.println("Type F:<face> or F:<face>,<loop|once|boomerang>");
    return;
  }
  String body = String(line + 2);
  String name = body, modeText = "";
  int comma = body.indexOf(',');
  if (comma >= 0) {
    name = body.substring(0, comma);
    modeText = body.substring(comma + 1);
  }
  name.trim();
  modeText.trim();
  FaceAnimMode mode;
  if (name.length() == 0 || !parseMode(modeText, mode)) {
    if (fromUsb) Serial.println("Bad face line");
    return;
  }

  // The body repeats the current face every few seconds so a rebooted head
  // catches up. A repeat must not restart the animation.
  if (name == requestedFace && mode == requestedMode) return;
  requestedFace = name;
  requestedMode = mode;
  showFace(name, mode);
  Serial.printf("Face: %s (%s)\n", name.c_str(), modeText.length() ? modeText.c_str() : "loop");
}

struct LineReader {
  Stream& in;
  bool fromUsb;
  char buf[48];
  uint8_t len;
  bool overflow;

  void poll() {
    while (in.available()) {
      char c = in.read();
      if (c == '\n' || c == '\r') {
        if (len > 0 && !overflow) {
          buf[len] = '\0';
          handleLine(buf, fromUsb);
        }
        len = 0;
        overflow = false;
      } else if (len < sizeof(buf) - 1) {
        buf[len++] = c;
      } else {
        overflow = true;  // drop the rest of an over-long line
      }
    }
  }
};

LineReader usbReader = { Serial, true, {}, 0, false };
LineReader linkReader = { Serial1, false, {}, 0, false };

// ====== MAIN ======
void setup() {
  Serial.begin(115200);
  Serial1.begin(LINK_BAUD, SERIAL_8N1, LINK_RX, LINK_TX);
  initDisplay();
  showFace("default", FACE_ANIM_LOOP);  // until the body says otherwise
  Serial.println("Sesame head ready. Type F:<face> to test.");
}

void loop() {
  linkReader.poll();
  usbReader.poll();
  updateAnimation();
  delay(2);
}
