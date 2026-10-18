// xiao-mapping-tester.ino - Seeed Studio XIAO ESP32-S3 servo wiring verifier.
//
// Confirms each servo INDEX in the Sesame firmware drives the physical servo
// you expect. It sweeps the 8 servos ONE AT A TIME (lazy attach + detach, so
// only ever one servo is powered) and announces each over Serial - watch the
// robot and confirm the wiring with zero ambiguity.
//
// Pin map matches sesame-firmware-main.ino with the head link (the default,
// XIAO_HEAD_LINK=1)  servoPins[] = {1,2,3,4,5,6,7,8}:
//   idx  servo  XIAO pin  GPIO
//    0    R1     D0          1
//    1    R2     D1          2
//    2    L1     D2          3
//    3    L2     D3          4
//    4    R4     D4          5
//    5    R3     D5          6
//    6    L3     D8          7
//    7    L4     D9          8
// D6/D7 are the head link and D10 is spare. For the older XIAO + OLED wiring,
// swap in the commented arrays below.
//
// Board: "XIAO_ESP32S3"  |  USB CDC On Boot: Enabled  |  Serial 115200
//
// Auto-sweeps 0 -> 7 on a loop. To re-test one servo, type its index (0-7)
// in the Serial Monitor.

#include <ESP32Servo.h>

const int   servoPins[8]   = {  1,    2,    3,    4,    5,    6,    7,    8  };
const char* servoLabels[8] = { "R1", "R2", "L1", "L2", "R4", "R3", "L3", "L4" };
const char* servoDpins[8]  = { "D0", "D1", "D2", "D3", "D4", "D5", "D8", "D9" };
// Older XIAO + OLED wiring (XIAO_HEAD_LINK=0):
//const int   servoPins[8]   = {  1,    4,    3,    2,    7,    9,   43,    8  };
//const char* servoDpins[8]  = { "D0", "D3", "D2", "D1", "D8", "D10","D6", "D9" };

const int CENTER = 90;   // test sweeps around the servo's centre
const int SWING  = 30;   // +/- degrees - gentle but clearly visible

Servo servo;             // single shared object: only one servo attached at a time

void testServo(int idx) {
  Serial.println();
  Serial.printf(">>> Index %d  =  %s   wired to %s (GPIO%d)\n",
                idx, servoLabels[idx], servoDpins[idx], servoPins[idx]);
  Serial.printf("    The %s servo should wiggle now - confirm it is the right one.\n",
                servoLabels[idx]);

  servo.setPeriodHertz(50);
  servo.attach(servoPins[idx], 732, 2929);   // same pulse range as the firmware

  servo.write(CENTER);
  delay(500);
  for (int rep = 0; rep < 3; rep++) {
    servo.write(CENTER - SWING); delay(450);
    servo.write(CENTER + SWING); delay(450);
  }
  servo.write(CENTER);
  delay(400);

  servo.detach();          // power this servo down before moving on
}

void printHeader() {
  Serial.println();
  Serial.println("========================================================");
  Serial.println(" XIAO ESP32-S3  -  Sesame servo mapping tester");
  Serial.println("========================================================");
  Serial.println(" idx  servo  XIAO  GPIO");
  for (int i = 0; i < 8; i++)
    Serial.printf("  %d    %-3s   %-3s   %d\n",
                  i, servoLabels[i], servoDpins[i], servoPins[i]);
  Serial.println(" Auto-sweeping 0..7. Type an index (0-7) to re-test just one.");
  Serial.println("========================================================");
}

void setup() {
  Serial.begin(115200);
  delay(400);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  printHeader();
}

void loop() {
  // Manual re-test: a digit 0-7 on Serial tests just that servo.
  if (Serial.available()) {
    char c = Serial.read();
    if (c >= '0' && c <= '7') testServo(c - '0');
    return;
  }

  // Auto-sweep: one servo per loop pass.
  static int idx = 0;
  testServo(idx);
  idx = (idx + 1) % 8;
  if (idx == 0) {
    Serial.println();
    Serial.println("--- full sweep complete - looping again ---");
  }
  delay(1200);   // pause between servos
}
