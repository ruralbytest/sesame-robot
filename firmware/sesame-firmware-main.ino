// --- XIAO head link ---
// On the Seeed XIAO ESP32-S3 the face lives on a separate head board
// (Waveshare ESP32-S3-Touch-AMOLED-1.43C), reached over UART on D6/D7,
// instead of an SSD1306 OLED. The body sends "F:<face>,<mode>" lines and
// accepts "C:<command>" lines back. Build with -DXIAO_HEAD_LINK=0 (or change
// the 1 below) to keep the older XIAO + OLED wiring and pin map.
#if defined(ARDUINO_XIAO_ESP32S3) && !defined(XIAO_HEAD_LINK)
#define XIAO_HEAD_LINK 1
#endif

// --- XIAO reversed body ---
// 1 = swap front and rear (the robot turned 180 degrees, head moved to the
// old rear end). No rewiring: each servo takes over the job of the servo
// diagonally across from it, which uses the same angles, so every pose and
// gait works unchanged toward the new front. Re-check subtrims after
// switching. 0 = the original orientation (kept as the backup).
#if defined(ARDUINO_XIAO_ESP32S3) && !defined(XIAO_REVERSE_BODY)
#define XIAO_REVERSE_BODY 0
#endif

// --- PCBCupid Glyph ESP32-S3 head link ---
// Auto-selected with the "Pcbcupid GLYPH S3" (or "... S3 PSRAM") board.
// Like the XIAO it defaults to the round head board, here on UART1
// (IO17 TX / IO18 RX). Build with -DGLYPH_HEAD_LINK=0 for the SSD1306 OLED
// on the Glyph's SDA/SCL pads (IO4/IO5) instead.
#if defined(ARDUINO_PCBCUPID_GLYPH_S3) || defined(ARDUINO_PCBCUPID_GLYPH_S3_PSRAM)
#define GLYPH_S3 1
#ifndef GLYPH_HEAD_LINK
#define GLYPH_HEAD_LINK 1
#endif
#else
#define GLYPH_S3 0
#endif

#if defined(ARDUINO_XIAO_ESP32S3) && XIAO_HEAD_LINK
#define HAS_HEAD_LINK 1
#define HAS_OLED 0
#define HEAD_RX_PIN 44   // D7, from the head's TXD (GPIO43)
#define HEAD_TX_PIN 43   // D6, to the head's RXD (GPIO44)
#define HEAD_BAUD 115200
#elif GLYPH_S3 && GLYPH_HEAD_LINK
#define HAS_HEAD_LINK 1
#define HAS_OLED 0
#define HEAD_RX_PIN 18   // IO18 (U1RXD), from the head's TXD (GPIO43)
#define HEAD_TX_PIN 17   // IO17 (U1TXD), to the head's RXD (GPIO44)
#define HEAD_BAUD 115200
#else
#define HAS_HEAD_LINK 0
#define HAS_OLED 1
#endif

// --- PCA9685 servo driver (optional, any board) ---
// Build with -DUSE_PCA9685=1 (or change the 0 below) to drive the 8 servos
// from a PCA9685 16-channel board on the I2C bus (I2C_SDA/I2C_SCL below;
// IO4/IO5 on the Glyph) instead of ESP32 GPIO PWM. Servo index i goes to
// channel pcaChannels[i]; channels 8-15 stay free. The OLED (0x3C) can share
// the bus. Needs the "Adafruit PWM Servo Driver Library".
#ifndef USE_PCA9685
#define USE_PCA9685 1
#endif
#define PCA9685_ADDR 0x40
// Real PCA9685 oscillators run near 27 MHz, not the nominal 25 MHz; trim
// this if servo angles are off on every channel by the same amount.
#define PCA9685_OSC_HZ 27000000
// Longest pulse sent to any servo. Some servos (the knees on at least one
// build) ignore anything above ~2400 us, so 175-180 deg never moved.
#define PCA_SERVO_MAX_US 2400

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#if HAS_OLED || USE_PCA9685
#include <Wire.h>
#endif
#if USE_PCA9685
#include <Adafruit_PWMServoDriver.h>
#else
#include <ESP32Servo.h>
#endif
#if HAS_OLED
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif
#include "face-bitmaps.h"
#include "movement-sequences.h"
#include "captive-portal.h"

// --- Access Point Configuration ---
// This is the network the Robot will create
#define AP_SSID  "Sesame-Controller"
#define AP_PASS  "12345678" // Must be at least 8 characters

// --- Station Mode Configuration (Optional) ---
// Set these to connect to your home/office WiFi network
// Leave NETWORK_SSID empty to disable station mode
#define NETWORK_SSID ""  // Your WiFi network name
#define NETWORK_PASS ""  // Your WiFi password
#define ENABLE_NETWORK_MODE false  // Set to true to enable network connection attempts

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_I2C_ADDR 0x3C

#if defined(ARDUINO_XIAO_ESP32S3)
// Seeed Studio XIAO ESP32-S3 (auto-selected when the XIAO_ESP32S3 board is chosen)
#define I2C_SDA 5   // D4 - XIAO default SDA
#define I2C_SCL 6   // D5 - XIAO default SCL
#elif GLYPH_S3
// PCBCupid Glyph ESP32-S3 (auto-selected) - board SDA/SCL pads
#define I2C_SDA 4
#define I2C_SCL 5
#else
// I2C Pins for Distro Board V2 / V3
//#define I2C_SDA 8
//#define I2C_SCL 9

// I2C Pins for Distro Board V1
//#define I2C_SDA 21
//#define I2C_SCL 22

// I2C Pins for S2 Mini Board
#define I2C_SDA 33
#define I2C_SCL 35
#endif


// DNS Server for Captive Portal
DNSServer dnsServer;
const byte DNS_PORT = 53;

#if HAS_OLED
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#endif
WebServer server(80);

// Global state for animations
String currentCommand = "";
String currentFaceName = "default";
const unsigned char* const* currentFaceFrames = nullptr;
uint8_t currentFaceFrameCount = 0;
uint8_t currentFaceFrameIndex = 0;
unsigned long lastFaceFrameMs = 0;
int faceFps = 8;
FaceAnimMode currentFaceMode = FACE_ANIM_LOOP;
int8_t faceFrameDirection = 1;
bool faceAnimFinished = false;
int currentFaceFps = 0;
bool idleActive = false;
bool idleBlinkActive = false;
unsigned long nextIdleBlinkMs = 0;
uint8_t idleBlinkRepeatsLeft = 0;

// WiFi Info Scrolling
unsigned long lastInputTime = 0;
bool firstInputReceived = false;
bool showingWifiInfo = false;
int wifiScrollPos = 0;
unsigned long lastWifiScrollMs = 0;
String wifiInfoText = "";

// Network Mode
bool networkConnected = false;
IPAddress networkIP;
String deviceHostname = "sesame-robot";
bool mdnsOk = false;

// Runtime WiFi provisioning (web UI) — the connect attempt runs as a state
// machine driven from loop() so HTTP handlers never block the captive portal.
enum WifiSetupState { WIFI_SETUP_IDLE, WIFI_SETUP_QUEUED, WIFI_SETUP_CONNECTING };
WifiSetupState wifiSetupState = WIFI_SETUP_IDLE;
String wifiSetupSsid = "";
String wifiSetupPass = "";
String wifiSetupError = "";          // result of the last finished attempt ("" = none/success)
unsigned long wifiSetupQueuedMs = 0;
unsigned long wifiSetupStartMs = 0;
bool wifiRestoreApOnly = false;      // drop the station iface again after an AP-only scan
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
const uint32_t WIFI_SETUP_START_DELAY_MS = 300;  // let the HTTP response flush before the AP channel may hop

// Servo Pins for Distro Board
// ======================================================================
// Pin numbers are coorisponding to the ESP32 GPIO pins and may differ based on which board you use.
// If you are using a different board, please adjust the servoPins array accordingly.
// ======================================================================
#if USE_PCA9685
// PCA9685 build: the servo signals come from the PCA9685, not GPIOs.
// R1=CH0 R2=CH1 L1=CH2 L2=CH3 R4=CH4 R3=CH5 L3=CH6 L4=CH7 (servo-index order)
Adafruit_PWMServoDriver pca(PCA9685_ADDR, Wire);
const uint8_t pcaChannels[8] = {0, 1, 2, 3, 4, 5, 6, 7};
#else
Servo servos[8];
#endif
#if USE_PCA9685
// servoPins is unused here; the GPIOs that held servos are free.
#elif defined(ARDUINO_XIAO_ESP32S3) && HAS_HEAD_LINK
// Seeed Studio XIAO ESP32-S3 with the round head board - all 8 servos run on
// LEDC hardware PWM via ESP32Servo. Hips on D0-D3, knees on D4/D5/D8/D9, in
// servo-index order, so the GPIOs run 1 to 8. D6/D7 carry the head link;
// no servo sits on a boot-log pin. D10 (GPIO9) is spare.
#if XIAO_REVERSE_BODY
// Reversed body: the same wires, roles swapped diagonally.
//   R1=D3  R2=D2  L1=D1  L2=D0  R4=D8  R3=D9  L3=D4  L4=D5
const int servoPins[8] = {4, 3, 2, 1, 7, 8, 5, 6};
#else
//   R1=D0  R2=D1  L1=D2  L2=D3  R4=D4  R3=D5  L3=D8  L4=D9
const int servoPins[8] = {1, 2, 3, 4, 5, 6, 7, 8};
#endif
#elif defined(ARDUINO_XIAO_ESP32S3)
// Seeed Studio XIAO ESP32-S3 with the SSD1306 OLED (XIAO_HEAD_LINK=0) - all
// 8 servos run on LEDC hardware PWM via ESP32Servo (the S3 has 8 LEDC
// channels). Pins match the older as-built wiring. OLED I2C is on D4/D5;
// D7 is free.
// L3 is on D6 (GPIO43, the UART0 boot-log pin) -> brief harmless twitch on
// reset; move L3 to the free D7 (GPIO44) and update this array to avoid it.
#if XIAO_REVERSE_BODY
// Reversed body: the same wires, roles swapped diagonally.
//   R1=D1  R2=D2  L1=D3  L2=D0  R4=D6  R3=D9  L3=D8  L4=D10
const int servoPins[8] = {2, 3, 4, 1, 43, 8, 7, 9};
#else
//   R1=D0  R2=D3  L1=D2  L2=D1  R4=D8  R3=D10  L3=D6  L4=D9
const int servoPins[8] = {1, 4, 3, 2, 7, 9, 43, 8};
#endif
#elif GLYPH_S3
// PCBCupid Glyph ESP32-S3 - same servo pins with or without the head link,
// in servo-index order. Kept off IO0 (boot strap), IO1 (battery sense, MSR),
// IO4/IO5 (I2C), IO17/IO18 (head link UART1), IO21 (LED) and IO43/IO44
// (UART0). Spare: IO2, IO16, IO35-37.
//   R1=IO6  R2=IO7  L1=IO8  L2=IO9  R4=IO10  R3=IO13  L3=IO14  L4=IO15
const int servoPins[8] = {6, 7, 8, 9, 10, 13, 14, 15};
#else
// Sesame Distro Board V3 Pinout [NEW]
//const int servoPins[8] = {4, 5, 6, 7, 10, 11, 12, 13};

// Sesame Distro Board V2 Pinout (Legacy)
//const int servoPins[8] = {4, 5, 6, 7, 15, 16, 17, 18};

// Sesame Distro Board V1 Pinout (Legacy)
//const int servoPins[8] = {15, 2, 23, 19, 4, 16, 17, 18};

// Lolin S2 Mini Pinout
const int servoPins[8] = {1, 2, 4, 6, 8, 10, 13, 14};
#endif

// Subtrim values for each servo (offset in degrees)
int8_t servoSubtrim[8] = {0, 0, 0, 0, 0, 0, 0, 0};


// Animation constants
int frameDelay = 100;
int walkCycles = 10;
int motorCurrentDelay = 20; // ms delay between motor movements to prevent over-current
bool servoLogEnabled = false; // Serial "log" toggle: print every servo write

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

struct FaceFpsEntry {
  const char* name;
  uint8_t fps;
};

const FaceFpsEntry faceFpsEntries[] = {
  { "walk", 1 },
  { "rest", 1 },
  { "swim", 1 },
  { "dance", 1 },
  { "wave", 1 },
  { "point", 5 },
  { "stand", 1 },
  { "cute", 1 },
  { "pushup", 1 },
  { "freaky", 1 },
  { "bow", 1 },
  { "worm", 1 },
  { "shake", 1 },
  { "shrug", 1 },
  { "dead", 2 },
  { "crab", 1 },
  { "idle", 1 },
  { "idle_blink", 7 },
  { "default", 1 },
  // Conversational faces (manually controlled by Python - no auto-animation)
  { "happy", 1 },
  { "talk_happy", 1 },
  { "sad", 1 },
  { "talk_sad", 1 },
  { "angry", 1 },
  { "talk_angry", 1 },
  { "surprised", 1 },
  { "talk_surprised", 1 },
  { "sleepy", 1 },
  { "talk_sleepy", 1 },
  { "love", 1 },
  { "talk_love", 1 },
  { "excited", 1 },
  { "talk_excited", 1 },
  { "confused", 1 },
  { "talk_confused", 1 },
  { "thinking", 1 },
  { "talk_thinking", 1 },
};


// Prototypes
void setServoAngle(uint8_t channel, int angle);
void updateFaceBitmap(const unsigned char* bitmap);
void setFace(const String& faceName);
void setFaceMode(FaceAnimMode mode);
void setFaceWithMode(const String& faceName, FaceAnimMode mode);
void updateAnimatedFace();
void delayWithFace(unsigned long ms);
void enterIdle();
void exitIdle();
void updateIdleBlink();
int getFaceFpsForName(const String& faceName);
bool pressingCheck(String cmd, int ms);
void handleGetSettings();
void handleSetSettings();
void handleGetStatus();
void handleApiCommand();
void updateWifiInfoScroll();
void recordInput();
bool connectToWifi(const String& ssid, const String& pass, uint32_t timeoutMs = 10000);
void handleWifiScan();
void handleWifiConnect();
void handleWifiStatus();
void handleNotFound();
String jsonEscape(const String& s);
bool startMdns();
void announceNetwork(const String& ssid);
void setApOnlyInfoText();
void showWifiInfoNow();
void updateWifiSetup();
void finishWifiSetup(const String& err);
void pollHeadLink();
void updateHeadLink();
void notifyHeadFace();
void syncHeadMove();

// ====== HEAD LINK ======
// One text line per message, ending in '\n'. Body -> head: "F:<face>,<mode>"
// (mode = loop | once | boomerang). Head -> body: "C:<command>", the same
// names as /cmd?pose= and /cmd?go=, plus "C:stop". Anything else (such as
// the head's boot log, which comes out on the same pins) is ignored.
// All traffic goes through sendToHead() and pollHeadLink(), so the link can
// later be swapped for ESP-NOW by changing only those two functions.
#if HAS_HEAD_LINK
String lastHeadFaceLine = "";
unsigned long lastHeadFaceSentMs = 0;
const unsigned long HEAD_FACE_RESEND_MS = 3000;  // re-sync a head that rebooted or was plugged in late

void sendToHead(const String& line) {
  Serial1.print(line);
  Serial1.print('\n');
}

const char* faceModeName(FaceAnimMode mode) {
  if (mode == FACE_ANIM_ONCE) return "once";
  if (mode == FACE_ANIM_BOOMERANG) return "boomerang";
  return "loop";
}

String headFaceLine() {
  return "F:" + currentFaceName + "," + faceModeName(currentFaceMode);
}

// Called whenever the face or its mode may have changed. Sends only on change;
// updateHeadLink() repeats the last line now and then, so the head must treat
// a repeat of the face it is already showing as a no-op.
void notifyHeadFace() {
  String line = headFaceLine();
  if (line == lastHeadFaceLine) return;
  lastHeadFaceLine = line;
  lastHeadFaceSentMs = millis();
  sendToHead(line);
}

// The move being run, sent as "M:<command>" ("M:idle" when none) so the head
// can show each move its own way. The face alone can't do that: forward,
// backward, left and right all use the "walk" face.
const char* const HEAD_MOVES[] = {
  "forward", "backward", "left", "right", "rest", "stand", "wave", "dance", "swim", "point",
  "pushup", "bow", "cute", "freaky", "worm", "shake", "shrug", "dead", "crab"
};
const char* lastHeadMove = nullptr;

void sendHeadMove() {
  Serial1.print("M:");
  Serial1.print(lastHeadMove);
  Serial1.print('\n');
}

// Called from pollHeadLink(), so it also runs during animations.
void syncHeadMove() {
  const char* move = "idle";
  for (const char* m : HEAD_MOVES) {
    if (currentCommand == m) { move = m; break; }
  }
  if (lastHeadMove != nullptr && strcmp(move, lastHeadMove) == 0) return;
  lastHeadMove = move;
  sendHeadMove();
}

void updateHeadLink() {
  if (lastHeadFaceLine.length() > 0 && millis() - lastHeadFaceSentMs >= HEAD_FACE_RESEND_MS) {
    lastHeadFaceSentMs = millis();
    sendToHead(lastHeadFaceLine);
    if (lastHeadMove != nullptr) sendHeadMove();
  }
}

void handleHeadLine(const char* line) {
  if (strncmp(line, "C:", 2) != 0) return;  // boot log or noise
  String cmd = String(line + 2);
  cmd.trim();
  if (cmd.length() == 0) return;
  Serial.print("Head command: ");
  Serial.println(cmd);
  recordInput();
  if (cmd == "stop") {
    currentCommand = "";
  } else {
    currentCommand = cmd;
    exitIdle();
  }
}

// Non-blocking: reads whatever bytes have arrived. Called from loop() and from
// the animation wait loops, so a tap on the head can interrupt a running pose.
void pollHeadLink() {
  syncHeadMove();
  static char buf[48];
  static uint8_t len = 0;
  static bool overflow = false;
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n' || c == '\r') {
      if (len > 0 && !overflow) {
        buf[len] = '\0';
        handleHeadLine(buf);
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
#else
void pollHeadLink() {}
void updateHeadLink() {}
void notifyHeadFace() {}
void syncHeadMove() {}
#endif

void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleCommandWeb() {
  // We send 200 OK immediately so the web browser doesn't hang waiting for animation to finish
  if (server.hasArg("pose")) {
    currentCommand = server.arg("pose");
    if (servoLogEnabled) Serial.printf("[web] t=%lu pose=%s\n", millis(), currentCommand.c_str());
    recordInput();
    exitIdle();
    server.send(200, "text/plain", "OK"); 
  } 
  else if (server.hasArg("go")) {
    currentCommand = server.arg("go");
    recordInput();
    exitIdle();
    server.send(200, "text/plain", "OK");
  } 
  else if (server.hasArg("stop")) {
    currentCommand = "";
    recordInput();
    server.send(200, "text/plain", "OK");
  }
  else if (server.hasArg("motor") && server.hasArg("value")) {
    int motorNum = server.arg("motor").toInt();
    int servoIdx = servoNameToIndex(server.arg("motor"));
    int angle = server.arg("value").toInt();
    if (motorNum >= 1 && motorNum <= 8 && angle >= 0 && angle <= 180) {
      setServoAngle(motorNum - 1, angle); // Convert 1-based to 0-based index
      recordInput();
      server.send(200, "text/plain", "OK");
    } else if (servoIdx != -1 && angle >= 0 && angle <= 180) {
      setServoAngle(servoIdx, angle);
      recordInput();
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Invalid motor or angle");
    }
  }
  else {
    server.send(400, "text/plain", "Bad Args");
  }
}

void handleGetSettings() {
  String json = "{";
  json += "\"frameDelay\":" + String(frameDelay) + ",";
  json += "\"walkCycles\":" + String(walkCycles) + ",";
  json += "\"motorCurrentDelay\":" + String(motorCurrentDelay) + ",";
  json += "\"faceFps\":" + String(faceFps);
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetSettings() {
  if (server.hasArg("frameDelay")) frameDelay = server.arg("frameDelay").toInt();
  if (server.hasArg("walkCycles")) walkCycles = server.arg("walkCycles").toInt();
  if (server.hasArg("motorCurrentDelay")) motorCurrentDelay = server.arg("motorCurrentDelay").toInt();
  if (server.hasArg("faceFps")) faceFps = (int)max(1L, server.arg("faceFps").toInt());
  server.send(200, "text/plain", "OK");
}

// API endpoint for network clients to get robot status
void handleGetStatus() {
  String json = "{";
  json += "\"currentCommand\":\"" + currentCommand + "\",";
  json += "\"currentFace\":\"" + currentFaceName + "\",";
  json += "\"networkConnected\":" + String(networkConnected ? "true" : "false") + ",";
  json += "\"apIP\":\"" + WiFi.softAPIP().toString() + "\"";
  if (networkConnected) {
    json += ",\"networkIP\":\"" + networkIP.toString() + "\"";
  }
  json += "}";
  server.send(200, "application/json", json);
}

// API endpoint for network clients to send commands (JSON-based)
void handleApiCommand() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
    return;
  }
  
  String body = server.arg("plain");
  
  Serial.println("API Command received:");
  Serial.println(body);
  
  // Check for face-only command (no movement)
  int faceOnlyStart = body.indexOf("\"face\":\"");
  if (faceOnlyStart == -1) {
    faceOnlyStart = body.indexOf("\"face\": \"");
  }
  
  // If we have a face but no command field, it's face-only
  bool faceOnly = (faceOnlyStart > 0 && body.indexOf("\"command\":") == -1 && body.indexOf("\"command\": ") == -1);
  
  String command = "";
  String face = "";
  
  // Parse face
  if (faceOnlyStart > 0) {
    faceOnlyStart = body.indexOf("\"", faceOnlyStart + 6) + 1;
    int faceEnd = body.indexOf("\"", faceOnlyStart);
    if (faceEnd > faceOnlyStart) {
      face = body.substring(faceOnlyStart, faceEnd);
      Serial.print("Parsed face: ");
      Serial.println(face);
    }
  }
  
  // Parse command (if not face-only)
  if (!faceOnly) {
    int cmdStart = body.indexOf("\"command\":\"");
    if (cmdStart == -1) {
      cmdStart = body.indexOf("\"command\": \"");
    }
    
    if (cmdStart == -1) {
      Serial.println("Error: command field not found");
      server.send(400, "application/json", "{\"error\":\"Missing command field\"}");
      return;
    }
    
    cmdStart = body.indexOf("\"", cmdStart + 10) + 1;
    int cmdEnd = body.indexOf("\"", cmdStart);
    
    if (cmdEnd <= cmdStart) {
      Serial.println("Error: invalid command format");
      server.send(400, "application/json", "{\"error\":\"Invalid command format\"}");
      return;
    }
    
    command = body.substring(cmdStart, cmdEnd);
    Serial.print("Parsed command: ");
    Serial.println(command);
  }
  
  // Set face if provided
  if (face.length() > 0) {
    setFace(face);
  }
  
  // If face-only, just acknowledge
  if (faceOnly) {
    recordInput();
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Face updated\"}");
    return;
  }
  
  // Execute command
  if (command == "stop") {
    currentCommand = "";
    recordInput();
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Command stopped\"}");
  } else {
    currentCommand = command;
    recordInput();
    exitIdle();
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Command executed\"}");
  }
}

// Escape a string for embedding in a JSON string literal. Handles backslash,
// double-quote, and control characters (SSIDs are arbitrary octets — a nearby
// network with a newline in its name must not break the whole response).
String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if ((uint8_t)c < 0x20) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", (uint8_t)c);
      out += buf;
    } else out += c;
  }
  return out;
}

// Start (or restart, after MDNS.end()) the mDNS responder. Tracks the result
// in mdnsOk so the API can avoid advertising a .local name that won't resolve.
bool startMdns() {
  mdnsOk = MDNS.begin(deviceHostname.c_str());
  if (mdnsOk) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS responder started: http://" + deviceHostname + ".local");
  } else {
    Serial.println("Error setting up mDNS responder!");
  }
  return mdnsOk;
}

// Post-join side effects shared by the boot path and the web provisioning
// path: re-announce mDNS on the new station interface and rebuild the OLED
// scroll text for the joined network.
void announceNetwork(const String& ssid) {
  MDNS.end();
  startMdns();
  wifiInfoText = "AP: " + String(AP_SSID) + " (" + WiFi.softAPIP().toString() +
                 ")  |  Network: " + ssid + " (" + networkIP.toString() + ") or " +
                 deviceHostname + ".local  |  ";
}

// OLED scroll text for AP-only operation (no station connection).
void setApOnlyInfoText() {
  wifiInfoText = "Connect to WiFi: " + String(AP_SSID) + "  |  Pass: " + String(AP_PASS) +
                 "  |  IP: " + WiFi.softAPIP().toString() + "  |  Captive Portal will auto-open!  |  ";
}

// Force the WiFi info scroll back on, even if the user has already driven the
// robot (recordInput() normally suppresses it permanently). Used after web
// provisioning so the new address is actually visible on the OLED.
void showWifiInfoNow() {
  firstInputReceived = false;
  lastInputTime = millis() - 30000;  // make the idle check pass immediately
  showingWifiInfo = false;           // let updateWifiInfoScroll re-init scroll state
}

// Blocking connect for the BOOT path only (the web server isn't running yet,
// so waiting here is harmless). Keeps the SoftAP alive via WIFI_AP_STA.
// Fast-fails on terminal states (wrong password / SSID not found) and stops
// background retries on failure. The web path uses updateWifiSetup() instead.
bool connectToWifi(const String& ssid, const String& pass, uint32_t timeoutMs) {
  if (ssid.length() == 0) return false;

  Serial.println("Connecting to WiFi network: " + ssid);
  WiFi.mode(WIFI_AP_STA);                 // keep AP up alongside station
  WiFi.setHostname(deviceHostname.c_str());
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long start = millis();
  while ((millis() - start) < timeoutMs) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) break;
    // Terminal states — waiting longer won't help.
    if ((millis() - start) > 1000 && (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL)) break;
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connect failed.");
    WiFi.disconnect();  // stop the driver retrying bad credentials in the background
    return false;
  }

  networkConnected = true;
  networkIP = WiFi.localIP();
  Serial.print("Connected! IP: ");
  Serial.println(networkIP);
  return true;
}

// Finish a web-initiated connect attempt (success or failure).
void finishWifiSetup(const String& err) {
  wifiSetupError = err;
  wifiSetupPass = "";  // don't keep the password in RAM longer than needed
  wifiSetupState = WIFI_SETUP_IDLE;
}

// Drives the non-blocking web connect from loop(). Also acts as a watchdog
// that keeps the cached networkConnected/networkIP honest if the router
// drops (or restores) the station link later.
void updateWifiSetup() {
  if (wifiSetupState == WIFI_SETUP_IDLE) {
    static unsigned long lastCheckMs = 0;
    if (millis() - lastCheckMs >= 5000) {
      lastCheckMs = millis();
      bool live = (WiFi.status() == WL_CONNECTED);
      if (live != networkConnected) {
        networkConnected = live;
        if (live) {
          networkIP = WiFi.localIP();
          announceNetwork(WiFi.SSID());
        } else {
          Serial.println("Station link lost.");
        }
      }
    }
    return;
  }

  if (wifiSetupState == WIFI_SETUP_QUEUED) {
    // Wait for the HTTP response to reach the client: joining a router on
    // another channel drags the SoftAP with it and deauths AP clients.
    if (millis() - wifiSetupQueuedMs < WIFI_SETUP_START_DELAY_MS) return;
    Serial.println("Connecting to WiFi network: " + wifiSetupSsid);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setHostname(deviceHostname.c_str());
    WiFi.begin(wifiSetupSsid.c_str(), wifiSetupPass.c_str());
    wifiSetupStartMs = millis();
    wifiSetupState = WIFI_SETUP_CONNECTING;
    return;
  }

  // WIFI_SETUP_CONNECTING
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    networkConnected = true;
    networkIP = WiFi.localIP();
    Serial.println("Connected! IP: " + networkIP.toString());
    announceNetwork(wifiSetupSsid);
    showWifiInfoNow();
    finishWifiSetup("");
    return;
  }
  // Grace period before trusting terminal states: right after WiFi.begin()
  // the status can still reflect the previous attempt.
  bool terminal = (millis() - wifiSetupStartMs > 1000) &&
                  (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL);
  if (terminal || (millis() - wifiSetupStartMs) >= WIFI_CONNECT_TIMEOUT_MS) {
    String err = (st == WL_NO_SSID_AVAIL)  ? "Network not found"
               : (st == WL_CONNECT_FAILED) ? "Wrong password or connection rejected"
                                           : "Connection timed out";
    Serial.println("WiFi connect failed: " + err);
    WiFi.disconnect();        // cancel the attempt; stop background retries
    networkConnected = false; // WiFi.begin() already tore down any previous link
    setApOnlyInfoText();      // OLED must not keep advertising a dead network IP
    finishWifiSetup(err);
  }
}

// GET /api/wifi/scan -> {"scanning":true} while the async scan runs, then a
// JSON array of nearby networks (raw; the UI dedups/sorts). Async keeps the
// captive portal responsive — the blocking scan stalls loop() for 2-4s.
void handleWifiScan() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    server.send(200, "application/json", "{\"scanning\":true}");
    return;
  }
  if (n < 0) {  // no scan results yet -> start one (unless a connect is mid-flight)
    if (wifiSetupState != WIFI_SETUP_IDLE) {
      server.send(200, "application/json", "{\"scanning\":true}");
      return;
    }
    if (WiFi.getMode() == WIFI_AP) {
      WiFi.mode(WIFI_AP_STA);   // station iface must be up to scan
      wifiRestoreApOnly = true; // drop it again once the scan is done
    }
    WiFi.scanNetworks(true /*async*/);
    server.send(200, "application/json", "{\"scanning\":true}");
    return;
  }

  String json = "[";
  json.reserve(n * 64 + 2);
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  json += "]";
  WiFi.scanDelete();
  // Restore the deliberate AP-only fallback if the scan was the only reason
  // the station interface came up.
  if (wifiRestoreApOnly && wifiSetupState == WIFI_SETUP_IDLE && WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_AP);
  }
  wifiRestoreApOnly = false;
  server.send(200, "application/json", json);
}

// POST /api/wifi/connect (form: ssid, password) -> {"success":true,"pending":true}.
// The attempt itself runs from loop() (updateWifiSetup) so this handler never
// blocks; the UI polls /api/wifi/status for the outcome.
void handleWifiConnect() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"success\":false,\"error\":\"Method not allowed\"}");
    return;
  }
  String ssid = server.arg("ssid");
  if (ssid.length() == 0) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"SSID required\"}");
    return;
  }
  if (wifiSetupState != WIFI_SETUP_IDLE) {
    server.send(409, "application/json", "{\"success\":false,\"error\":\"Connection attempt already in progress\"}");
    return;
  }

  wifiSetupSsid = ssid;
  wifiSetupPass = server.arg("password");
  wifiSetupError = "";
  wifiSetupQueuedMs = millis();
  wifiSetupState = WIFI_SETUP_QUEUED;
  wifiRestoreApOnly = false;  // an explicit connect supersedes scan cleanup
  server.send(200, "application/json", "{\"success\":true,\"pending\":true}");
}

// GET /api/wifi/status -> station state for the Settings panel, including
// in-progress attempts and the last error so the UI can poll for the result.
void handleWifiStatus() {
  bool connected = (WiFi.status() == WL_CONNECTED);
  String json = "{\"connected\":" + String(connected ? "true" : "false");
  json += ",\"connecting\":" + String(wifiSetupState != WIFI_SETUP_IDLE ? "true" : "false");
  if (wifiSetupError.length() > 0) {
    json += ",\"lastError\":\"" + jsonEscape(wifiSetupError) + "\"";
  }
  if (connected) {
    json += ",\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\"";
    json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    json += ",\"host\":\"" + deviceHostname + ".local\"";
    json += ",\"mdns\":" + String(mdnsOk ? "true" : "false");
    json += ",\"rssi\":" + String(WiFi.RSSI());
  }
  json += "}";
  server.send(200, "application/json", json);
}

// Unmatched routes: JSON 404 for API paths (a typo'd /api/* URL must not get
// 200 + portal HTML), captive-portal redirect for everything else.
void handleNotFound() {
  if (server.uri().startsWith("/api/")) {
    server.send(404, "application/json", "{\"error\":\"Not found\"}");
    return;
  }
  handleRoot();
}

void setup() {
  Serial.begin(115200);
  randomSeed(micros());

#if HAS_HEAD_LINK
  // Link to the head board. The robot runs fine with no head attached.
  Serial1.begin(HEAD_BAUD, SERIAL_8N1, HEAD_RX_PIN, HEAD_TX_PIN);
#endif

#if HAS_OLED || USE_PCA9685
  // I2C Init for ESP32 (OLED and/or PCA9685)
  Wire.begin(I2C_SDA, I2C_SCL);
#endif

#if HAS_OLED
  // OLED Init
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println(F("SSD1306 allocation failed."));
    while (1);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println(F("Setting up WiFi..."));
  display.display();
#endif

  // --- WIFI CONFIGURATION ---
  // Don't write credentials to NVS flash: runtime WiFi setup is session-only
  // (see firmware/README.md), and passwords shouldn't persist silently.
  WiFi.persistent(false);

  // Try to connect to network first if configured
  if (ENABLE_NETWORK_MODE && String(NETWORK_SSID).length() > 0) {
    if (!connectToWifi(NETWORK_SSID, NETWORK_PASS)) {
      Serial.println("Failed to connect to network. Running in AP-only mode.");
      WiFi.mode(WIFI_AP); // Fall back to AP-only
    }
  } else {
    WiFi.mode(WIFI_AP);
    Serial.println("Network mode disabled. Running in AP-only mode.");
  }
  
  // --- ACCESS POINT CONFIGURATION ---
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress myIP = WiFi.softAPIP();
  
  Serial.print("AP Created. IP: ");
  Serial.println(myIP);

  // Build WiFi info text for scrolling + start mDNS responder
  if (networkConnected) {
    announceNetwork(NETWORK_SSID);
  } else {
    setApOnlyInfoText();
    startMdns();
  }

  // Initialize input tracking
  lastInputTime = millis();
  firstInputReceived = false;
  showingWifiInfo = false;

  // Start DNS Server for Captive Portal
  // This redirects ALL domain requests to the ESP32's IP
  dnsServer.start(DNS_PORT, "*", myIP);

  // Web Server Routes
  server.on("/", handleRoot);
  server.on("/cmd", handleCommandWeb);
  server.on("/getSettings", handleGetSettings);
  server.on("/setSettings", handleSetSettings);
  
  // API endpoints for network communication
  server.on("/api/status", handleGetStatus);
  server.on("/api/command", handleApiCommand);

  // WiFi provisioning endpoints (runtime network setup from the web UI)
  server.on("/api/wifi/scan", handleWifiScan);
  server.on("/api/wifi/connect", handleWifiConnect);
  server.on("/api/wifi/status", handleWifiStatus);
  
  // Catch-all route for captive portal
  // This ensures any URL redirects to the controller page
  // (except /api/* paths, which get a JSON 404 — see handleNotFound)
  server.onNotFound(handleNotFound);
  
  server.begin();

#if USE_PCA9685
  // PCA9685 Init: 50 Hz servo frames. Outputs stay off (servos limp) until
  // the first setServoAngle(), the same as the GPIO build.
  Wire.beginTransmission(PCA9685_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.println(F("PCA9685 not found at 0x40 - check SDA/SCL, VCC and GND."));
  }
  pca.begin();
  pca.setOscillatorFrequency(PCA9685_OSC_HZ);
  pca.setPWMFreq(50);
#else
  // PWM Init
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  for (int i = 0; i < 8; i++) {
    servos[i].setPeriodHertz(50);
    // Map 0-180 to approx 732-2929us (ESP32Servo clamps the max to 2500us)
    servos[i].attach(servoPins[i], 732, 2929);
  }
#endif
  delay(10);
  
  // Show rest face on startup without moving motors
  setFace("rest");
  
  Serial.println(F("HTTP server & Captive Portal started."));
}

void loop() {
  // Process DNS requests for captive portal
  dnsServer.processNextRequest();
  
  server.handleClient();
  updateWifiSetup();
  pollHeadLink();
  updateAnimatedFace();
  updateIdleBlink();
  updateWifiInfoScroll();
  updateHeadLink();

  syncHeadMove();  // tell the head before a one-shot pose clears the command
  if (currentCommand != "") {
    String cmd = currentCommand;
    if (cmd == "forward") runWalkPose();
    else if (cmd == "backward") runWalkBackward();
    else if (cmd == "left") runTurnLeft();
    else if (cmd == "right") runTurnRight();
    else if (cmd == "rest") { runRestPose(); if (currentCommand == "rest") currentCommand = ""; }
    else if (cmd == "stand") { runStandPose(1); if (currentCommand == "stand") currentCommand = ""; }
    else if (cmd == "wave") runWavePose();
    else if (cmd == "dance") runDancePose();
    else if (cmd == "swim") runSwimPose();
    else if (cmd == "point") runPointPose();
    else if (cmd == "pushup") runPushupPose();
    else if (cmd == "bow") runBowPose();
    else if (cmd == "cute") runCutePose();
    else if (cmd == "freaky") runFreakyPose();
    else if (cmd == "worm") runWormPose();
    else if (cmd == "shake") runShakePose();
    else if (cmd == "shrug") runShrugPose();
    else if (cmd == "dead") runDeadPose();
    else if (cmd == "crab") runCrabPose();
  }
  
  // Serial CLI for debugging (can be used to diagnose servo position issues and wiring)
  if (Serial.available()) {
    static char command_buffer[32];
    static byte buffer_pos = 0;
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (buffer_pos > 0) {
        command_buffer[buffer_pos] = '\0';
        int motorNum, angle;
        recordInput();
        if(strcmp(command_buffer, "run walk") == 0 || strcmp(command_buffer, "rn wf") == 0) { currentCommand = "forward"; runWalkPose(); currentCommand = ""; }
        else if(strcmp(command_buffer, "rn wb") == 0) { currentCommand = "backward"; runWalkBackward(); currentCommand = ""; }
        else if(strcmp(command_buffer, "rn tl") == 0) { currentCommand = "left"; runTurnLeft(); currentCommand = ""; }
        else if(strcmp(command_buffer, "rn tr") == 0) { currentCommand = "right"; runTurnRight(); currentCommand = ""; }
        else if(strcmp(command_buffer, "run rest") == 0 || strcmp(command_buffer, "rn rs") == 0) { currentCommand = "rest"; syncHeadMove(); runRestPose(); currentCommand = ""; }
        else if(strcmp(command_buffer, "run stand") == 0 || strcmp(command_buffer, "rn st") == 0) { currentCommand = "stand"; syncHeadMove(); runStandPose(1); currentCommand = ""; }
        else if(strcmp(command_buffer, "rn wv") == 0) { currentCommand = "wave"; runWavePose(); }
        else if(strcmp(command_buffer, "rn dn") == 0) { currentCommand = "dance"; runDancePose(); }
        else if(strcmp(command_buffer, "rn sw") == 0) { currentCommand = "swim"; runSwimPose(); }
        else if(strcmp(command_buffer, "rn pt") == 0) { currentCommand = "point"; runPointPose(); }
        else if(strcmp(command_buffer, "rn pu") == 0) { currentCommand = "pushup"; runPushupPose(); }
        else if(strcmp(command_buffer, "rn bw") == 0) { currentCommand = "bow"; runBowPose(); }
        else if(strcmp(command_buffer, "rn ct") == 0) { currentCommand = "cute"; runCutePose(); }
        else if(strcmp(command_buffer, "rn fk") == 0) { currentCommand = "freaky"; runFreakyPose(); }
        else if(strcmp(command_buffer, "rn wm") == 0) { currentCommand = "worm"; runWormPose(); }
        else if(strcmp(command_buffer, "rn sk") == 0) { currentCommand = "shake"; runShakePose(); }
        else if(strcmp(command_buffer, "rn sg") == 0) { currentCommand = "shrug"; runShrugPose(); }
        else if(strcmp(command_buffer, "rn dd") == 0) { currentCommand = "dead"; runDeadPose(); }
        else if(strcmp(command_buffer, "rn cb") == 0) { currentCommand = "crab"; runCrabPose(); }
        else if (strncmp(command_buffer, "face ", 5) == 0 || strncmp(command_buffer, "fc ", 3) == 0) {
          const char* faceName = (command_buffer[1] == 'c') ? command_buffer + 3 : command_buffer + 5;
          if (strlen(faceName) > 0) {
            currentCommand = "";
            setFace(String(faceName));
            Serial.print("Face set to ");
            Serial.println(faceName);
          } else {
            Serial.println("Usage: face <name>");
          }
        }
        else if (strcmp(command_buffer, "subtrim") == 0 || strcmp(command_buffer, "st") == 0) {
          Serial.println("Subtrim values:");
          for (int i = 0; i < 8; i++) {
            Serial.print("Motor "); Serial.print(i); Serial.print(": ");
            if (servoSubtrim[i] >= 0) Serial.print("+");
            Serial.println(servoSubtrim[i]);
          }
        }
        else if (strcmp(command_buffer, "subtrim save") == 0 || strcmp(command_buffer, "st save") == 0) {
          Serial.println("Copy and paste this into your code:");
          Serial.print("int8_t servoSubtrim[8] = {");
          for (int i = 0; i < 8; i++) {
            Serial.print(servoSubtrim[i]);
            if (i < 7) Serial.print(", ");
          }
          Serial.println("};");
        }
        else if (strncmp(command_buffer, "subtrim reset", 13) == 0 || strncmp(command_buffer, "st reset", 8) == 0) {
          for (int i = 0; i < 8; i++) servoSubtrim[i] = 0;
          Serial.println("All subtrim values reset to 0");
        }
        else if (strncmp(command_buffer, "subtrim ", 8) == 0 || strncmp(command_buffer, "st ", 3) == 0) {
          const char* params = (command_buffer[1] == 't') ? command_buffer + 3 : command_buffer + 8;
          int trimMotor, trimValue;
          if (sscanf(params, "%d %d", &trimMotor, &trimValue) == 2) {
            if (trimMotor >= 0 && trimMotor < 8) {
              if (trimValue >= -90 && trimValue <= 90) {
                servoSubtrim[trimMotor] = trimValue;
                Serial.print("Motor "); Serial.print(trimMotor); Serial.print(" subtrim set to ");
                if (trimValue >= 0) Serial.print("+");
                Serial.println(trimValue);
              } else {
                Serial.println("Subtrim value must be between -90 and +90");
              }
            } else {
              Serial.println("Invalid motor number (0-7)");
            }
          }
        }
        else if (strcmp(command_buffer, "log") == 0) {
          servoLogEnabled = !servoLogEnabled;
          Serial.print("Servo log "); Serial.println(servoLogEnabled ? "on" : "off");
        }
#if USE_PCA9685
        else if (strcmp(command_buffer, "pca") == 0) {
          // Dump the chip state. A brownout reset shows as prescale 30
          // (200 Hz default) with every channel off.
          Wire.beginTransmission(PCA9685_ADDR); Wire.write(0x00); Wire.endTransmission();
          Wire.requestFrom((uint8_t)PCA9685_ADDR, (uint8_t)1);
          uint8_t mode1 = Wire.available() ? Wire.read() : 0xFF;
          Serial.printf("PCA9685 mode1=0x%02X prescale=%u (expect %u)\n",
                        mode1, pca.readPrescale(),
                        (unsigned)(PCA9685_OSC_HZ / (4096.0 * 50.0) + 0.5) - 1);
          for (uint8_t ch = 0; ch < 16; ch++) {
            uint16_t on = pca.getPWM(ch, false), off = pca.getPWM(ch, true);
            Serial.printf("  ch%u on=%u off=%u\n", ch, on, off);
          }
        }
#endif
        else if (strncmp(command_buffer, "all ", 4) == 0) {
             if (sscanf(command_buffer + 4, "%d", &angle) == 1) {
                 for (int i = 0; i < 8; i++) setServoAngle(i, angle);
                 Serial.print("All servos set to "); Serial.println(angle);
             }
        }
        else if (sscanf(command_buffer, "%d %d", &motorNum, &angle) == 2) {
             if (motorNum >= 0 && motorNum < 8) {
                 setServoAngle(motorNum, angle);
                 Serial.print("Servo "); Serial.print(motorNum); Serial.print(" set to "); Serial.println(angle);
             } else {
                 Serial.println("Invalid motor number (0-7)");
             }
        }
        buffer_pos = 0;
      }
    } else if (buffer_pos < sizeof(command_buffer) - 1) {
      command_buffer[buffer_pos++] = c;
    }
  }
}

// Function to update the robot's face
// With the head link, the head draws and animates the faces itself. The body
// still steps through the frames (without drawing) so ONCE-mode timing, such
// as the idle blink, finishes at the same moment on both boards.
void updateFaceBitmap(const unsigned char* bitmap) {
#if HAS_OLED
  display.clearDisplay();
  display.drawBitmap(0, 0, bitmap, 128, 64, SSD1306_WHITE);
  display.display();
#else
  (void)bitmap;
#endif
}

uint8_t countFrames(const unsigned char* const* frames, uint8_t maxFrames) {
  if (frames == nullptr || frames[0] == nullptr) return 0;
  uint8_t count = 0;
  for (uint8_t i = 0; i < maxFrames; i++) {
    if (frames[i] == nullptr) break;
    count++;
  }
  return count;
}

void setFace(const String& faceName) {
  if (faceName == currentFaceName && currentFaceFrames != nullptr) {
    notifyHeadFace();  // same face, but setFaceWithMode() may have changed the mode
    return;
  }

  currentFaceName = faceName;
  currentFaceFrameIndex = 0;
  lastFaceFrameMs = 0;
  faceFrameDirection = 1;
  faceAnimFinished = false;
  currentFaceFps = getFaceFpsForName(faceName);

  currentFaceFrames = face_defualt_frames;
  currentFaceFrameCount = countFrames(face_defualt_frames, MAX_FACE_FRAMES);

  for (size_t i = 0; i < (sizeof(faceEntries) / sizeof(faceEntries[0])); i++) {
    if (faceName.equalsIgnoreCase(faceEntries[i].name)) {
      currentFaceFrames = faceEntries[i].frames;
      currentFaceFrameCount = countFrames(faceEntries[i].frames, faceEntries[i].maxFrames);
      break;
    }
  }

  if (currentFaceFrameCount == 0) {
    currentFaceFrames = face_defualt_frames;
    currentFaceFrameCount = countFrames(face_defualt_frames, MAX_FACE_FRAMES);
    currentFaceName = "default";
    currentFaceFps = getFaceFpsForName(currentFaceName);
  }

  if (currentFaceFrameCount > 0 && currentFaceFrames[0] != nullptr) {
    updateFaceBitmap(currentFaceFrames[0]);
  }
  notifyHeadFace();
}

void setFaceMode(FaceAnimMode mode) {
  currentFaceMode = mode;
  faceFrameDirection = 1;
  faceAnimFinished = false;
}

void setFaceWithMode(const String& faceName, FaceAnimMode mode) {
  setFaceMode(mode);
  setFace(faceName);
}

int getFaceFpsForName(const String& faceName) {
  for (size_t i = 0; i < (sizeof(faceFpsEntries) / sizeof(faceFpsEntries[0])); i++) {
    if (faceName.equalsIgnoreCase(faceFpsEntries[i].name)) {
      return faceFpsEntries[i].fps;
    }
  }
  return faceFps;
}

void updateAnimatedFace() {
  if (currentFaceFrames == nullptr || currentFaceFrameCount <= 1) return;
  if (currentFaceMode == FACE_ANIM_ONCE && faceAnimFinished) return;

  unsigned long now = millis();
  int fps = max(1, (currentFaceFps > 0 ? currentFaceFps : faceFps));
  unsigned long interval = 1000UL / fps;
  if (now - lastFaceFrameMs >= interval) {
    lastFaceFrameMs = now;
    if (currentFaceMode == FACE_ANIM_LOOP) {
      currentFaceFrameIndex = (currentFaceFrameIndex + 1) % currentFaceFrameCount;
    } else if (currentFaceMode == FACE_ANIM_ONCE) {
      if (currentFaceFrameIndex + 1 >= currentFaceFrameCount) {
        currentFaceFrameIndex = currentFaceFrameCount - 1;
        faceAnimFinished = true;
      } else {
        currentFaceFrameIndex++;
      }
    } else {
      if (faceFrameDirection > 0) {
        if (currentFaceFrameIndex + 1 >= currentFaceFrameCount) {
          faceFrameDirection = -1;
          if (currentFaceFrameIndex > 0) currentFaceFrameIndex--;
        } else {
          currentFaceFrameIndex++;
        }
      } else {
        if (currentFaceFrameIndex == 0) {
          faceFrameDirection = 1;
          if (currentFaceFrameCount > 1) currentFaceFrameIndex++;
        } else {
          currentFaceFrameIndex--;
        }
      }
    }
    updateFaceBitmap(currentFaceFrames[currentFaceFrameIndex]);
  }
}

void delayWithFace(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    updateAnimatedFace();
    server.handleClient();
    dnsServer.processNextRequest();
    pollHeadLink();
    delay(5);
  }
}

void scheduleNextIdleBlink(unsigned long minMs, unsigned long maxMs) {
  unsigned long now = millis();
  unsigned long interval = (unsigned long)random(minMs, maxMs);
  nextIdleBlinkMs = now + interval;
}

void enterIdle() {
  idleActive = true;
  idleBlinkActive = false;
  idleBlinkRepeatsLeft = 0;
  setFaceWithMode("idle", FACE_ANIM_BOOMERANG);
  scheduleNextIdleBlink(3000, 7000);
}

void exitIdle() {
  idleActive = false;
  idleBlinkActive = false;
}

void updateIdleBlink() {
  if (!idleActive) return;

  if (!idleBlinkActive) {
    if (millis() >= nextIdleBlinkMs) {
      idleBlinkActive = true;
      if (idleBlinkRepeatsLeft == 0 && random(0, 100) < 30) {
        idleBlinkRepeatsLeft = 1; // double blink
      }
      setFaceWithMode("idle_blink", FACE_ANIM_ONCE);
    }
    return;
  }

  if (currentFaceMode == FACE_ANIM_ONCE && faceAnimFinished) {
    idleBlinkActive = false;
    setFaceWithMode("idle", FACE_ANIM_BOOMERANG);
    if (idleBlinkRepeatsLeft > 0) {
      idleBlinkRepeatsLeft--;
      scheduleNextIdleBlink(120, 220);
    } else {
      scheduleNextIdleBlink(3000, 7000);
    }
  }
}

// ====== HELPERS ======
void setServoAngle(uint8_t channel, int angle) { 
  if (channel < 8) {
    int adjustedAngle = constrain(angle + servoSubtrim[channel], 0, 180);
#if USE_PCA9685
    // Match what the ESP32Servo build really outputs: attach(732, 2929) is
    // clamped by the library to MAX_PULSE_WIDTH = 2500 us, so 180 deg is
    // 2500 us there. Sending 2929 us here left the knees (R3/L4 at 180 in
    // stand) stalled or rejecting the pulse.
    // Many hobby servos ignore pulses above ~2400 us (they just hold), so cap
    // there: 170-180 deg all land on the servo's real end of travel.
    int us = min(map(adjustedAngle, 0, 180, 732, 2500), (long)PCA_SERVO_MAX_US);
    pca.writeMicroseconds(pcaChannels[channel], us);
    if (servoLogEnabled) {
      // Read the OFF count back from the chip so the log proves what the
      // PCA9685 is actually outputting, not just what we asked for.
      uint16_t off = pca.getPWM(pcaChannels[channel], true);
      Serial.printf("[servo] t=%lu idx %u ch %u angle %d -> %d us (off=%u)\n",
                    millis(), channel, pcaChannels[channel], adjustedAngle, us, off);
    }
#else
    servos[channel].write(adjustedAngle);
    if (servoLogEnabled) {
      Serial.printf("[servo] idx %u pin %u angle %d\n", channel, servoPins[channel], adjustedAngle);
    }
#endif
    delayWithFace(motorCurrentDelay);
  }
}

bool pressingCheck(String cmd, int ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    server.handleClient();
    dnsServer.processNextRequest();
    pollHeadLink();
    updateAnimatedFace();
    if (currentCommand != cmd) {
      runStandPose(1);
      return false;
    }
    yield();
  }
  return true;
}

void recordInput() {
  lastInputTime = millis();
  if (!firstInputReceived) {
    firstInputReceived = true;
    showingWifiInfo = false;
  }
}

void updateWifiInfoScroll() {
#if !HAS_OLED
  return;  // no screen on the body; the WiFi details are on the Serial Monitor
#else
  // Don't show WiFi info if first input has been received
  if (firstInputReceived) {
    if (showingWifiInfo) {
      showingWifiInfo = false;
      // Restore the current face
      if (currentFaceFrames != nullptr && currentFaceFrameCount > 0) {
        updateFaceBitmap(currentFaceFrames[currentFaceFrameIndex]);
      }
    }
    return;
  }
  
  unsigned long now = millis();
  
  // Check if 30 seconds have passed without input
  if (!showingWifiInfo && (now - lastInputTime >= 30000)) {
    showingWifiInfo = true;
    wifiScrollPos = 0;
    lastWifiScrollMs = now;
  }
  
  if (!showingWifiInfo) return;
  
  // Update scroll every 150ms
  if (now - lastWifiScrollMs >= 150) {
    lastWifiScrollMs = now;
    
    // Clear and redraw with current face in background
    display.clearDisplay();
    
    // Draw the face bitmap in the background
    if (currentFaceFrames != nullptr && currentFaceFrameCount > 0) {
      display.drawBitmap(0, 0, currentFaceFrames[currentFaceFrameIndex], 128, 64, SSD1306_WHITE);
    }
    
    // Draw black bar for text background on top row
    display.fillRect(0, 0, 128, 10, SSD1306_BLACK);
    
    // Draw scrolling text
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setTextWrap(false);
    display.setCursor(-wifiScrollPos, 1);
    display.print(wifiInfoText);
    display.setTextWrap(true);
    
    display.display();
    
    // Advance scroll position
    wifiScrollPos += 2;
    if (wifiScrollPos >= (int)(wifiInfoText.length() * 6)) {
      wifiScrollPos = 0;
    }
  }
#endif
}
