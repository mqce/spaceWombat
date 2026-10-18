#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

#include "SpaceMouseReport.h"

namespace {

constexpr char kDeviceName[] = "SpaceMouse Pro Wireless";
constexpr int kLedPin = 8;
constexpr bool kLedActiveLow = true;
constexpr uint32_t kReportIntervalMs = 16;

constexpr int kJoyPinX = A4;
constexpr int kJoyPinY = A3;
constexpr int kJoyAdcMax = 4095;
constexpr int kJoySamples = 5;
constexpr int kJoyCalibrationSamples = 16;
constexpr int kJoyDeadzone = 280;
constexpr int kJoySpike = 250;
constexpr int kJoySpikeHold = 3;
constexpr bool kJoyInvertX = false;
constexpr bool kJoyInvertY = false;
constexpr uint32_t kJoyLogIntervalMs = 500;

enum class InputSource { kJoystick, kManual, kDemo };

struct JoyAxis {
  int pin;
  bool invert;
  int center;
  int raw;
  int value;
  int candidate;
  int hits;
  bool ready;
};

JoyAxis joyX = {kJoyPinX, kJoyInvertX, kJoyAdcMax / 2};
JoyAxis joyY = {kJoyPinY, kJoyInvertY, kJoyAdcMax / 2};
bool joyLog = false;

NimBLEHIDDevice* hid = nullptr;
NimBLECharacteristic* axisReport = nullptr;
NimBLECharacteristic* buttonReport = nullptr;

struct LinkState {
  bool connected;
  bool encrypted;
  bool axisSubscribed;
  bool buttonSubscribed;
  uint8_t led;
};

volatile LinkState conn = {};
AxisReport axes = {};
uint32_t buttons = 0;
uint8_t battery = 100;
InputSource source = InputSource::kJoystick;
char line[96];
size_t lineLen = 0;
SET_LOOP_TASK_STACK_SIZE(16384);

void setBoardLed(bool on) {
  const bool level = on ^ kLedActiveLow;
  digitalWrite(kLedPin, level ? HIGH : LOW);
}

int16_t clampAxis(long value) {
  if (value < kAxisMin) {
    return kAxisMin;
  }
  if (value > kAxisMax) {
    return kAxisMax;
  }
  return static_cast<int16_t>(value);
}

int readMedian(int pin) {
  int samples[kJoySamples];
  for (int i = 0; i < kJoySamples; ++i) {
    const int sample = analogRead(pin);
    int j = i;
    while (j > 0 && samples[j - 1] > sample) {
      samples[j] = samples[j - 1];
      --j;
    }
    samples[j] = sample;
    delayMicroseconds(250);
  }
  return samples[kJoySamples / 2];
}

void filterJoyAxis(JoyAxis& axis, int sample) {
  axis.raw = sample;
  if (!axis.ready) {
    axis.value = sample;
    axis.hits = 0;
    axis.ready = true;
    return;
  }

  if (abs(sample - axis.value) <= kJoySpike) {
    axis.value = (axis.value * 3 + sample) / 4;
    axis.hits = 0;
    return;
  }

  if (axis.hits > 0 && abs(sample - axis.candidate) <= kJoySpike) {
    axis.candidate = (axis.candidate * 3 + sample) / 4;
    ++axis.hits;
  } else {
    axis.candidate = sample;
    axis.hits = 1;
  }
  if (axis.hits >= kJoySpikeHold) {
    axis.value = axis.candidate;
    axis.hits = 0;
  }
}

int16_t joyAxisToReport(const JoyAxis& axis) {
  int delta = axis.value - axis.center;
  if (axis.invert) {
    delta = -delta;
  }
  if (abs(delta) <= kJoyDeadzone) {
    return 0;
  }

  const bool positive = delta > 0;
  const int span = positive ? kJoyAdcMax - axis.center : axis.center;
  const int usable = max(1, span - kJoyDeadzone);
  const long travel = abs(delta) - kJoyDeadzone;
  const long scaled = travel * kAxisMax / usable;
  return clampAxis(positive ? scaled : -scaled);
}

void calibrateJoyAxis(JoyAxis& axis) {
  long sum = 0;
  for (int i = 0; i < kJoyCalibrationSamples; ++i) {
    sum += readMedian(axis.pin);
    delay(2);
  }
  axis.center = static_cast<int>(sum / kJoyCalibrationSamples);
  axis.ready = false;
}

void calibrateJoystick() {
  calibrateJoyAxis(joyX);
  calibrateJoyAxis(joyY);
  Serial.print("joy center X=");
  Serial.print(joyX.center);
  Serial.print(" Y=");
  Serial.println(joyY.center);
}

void printJoyLog(uint32_t now) {
  static uint32_t lastLogMs = 0;
  if (!joyLog || now - lastLogMs < kJoyLogIntervalMs) {
    return;
  }
  lastLogMs = now;
  Serial.print("joy X raw=");
  Serial.print(joyX.raw);
  Serial.print(" filt=");
  Serial.print(joyX.value);
  Serial.print(" Y raw=");
  Serial.print(joyY.raw);
  Serial.print(" filt=");
  Serial.print(joyY.value);
  Serial.print(" -> axes x=");
  Serial.print(axes.x);
  Serial.print(" z=");
  Serial.println(axes.z);
}

void updateJoystick(uint32_t now) {
  filterJoyAxis(joyX, readMedian(joyX.pin));
  filterJoyAxis(joyY, readMedian(joyY.pin));
  axes = {};
  axes.x = joyAxisToReport(joyX);
  axes.z = joyAxisToReport(joyY);
  printJoyLog(now);
}

bool isZero(const AxisReport& report) {
  return report.x == 0 && report.y == 0 && report.z == 0 &&
         report.rx == 0 && report.ry == 0 && report.rz == 0;
}

const char* sourceName() {
  switch (source) {
    case InputSource::kJoystick:
      return "joystick";
    case InputSource::kManual:
      return "manual";
    case InputSource::kDemo:
      return "demo";
  }
  return "?";
}

const char* skipSpace(const char* text) {
  while (*text == ' ' || *text == '\t') {
    ++text;
  }
  return text;
}

bool nextInt(const char*& text, long& value) {
  text = skipSpace(text);
  if (*text == '\0') {
    return false;
  }
  char* end = nullptr;
  value = strtol(text, &end, 0);
  if (end == text) {
    return false;
  }
  text = end;
  return true;
}

void printUsage() {
  Serial.println("A x y z rx ry rz   軸 -350..350");
  Serial.println("B <hex>            ボタン 32bit");
  Serial.println("L <0-100>          電池残量");
  Serial.println("Z                  軸とボタンをゼロ、ジョイスティックに戻す");
  Serial.println("D                  デモ開始/停止");
  Serial.println("J                  ジョイスティック中心を再測定");
  Serial.println("V                  ジョイスティックログ切替");
  Serial.println("P                  状態");
}

void printState() {
  const bool connected = conn.connected;
  const bool encrypted = conn.encrypted;
  Serial.print("connected=");
  Serial.print(connected ? 1 : 0);
  Serial.print(" encrypted=");
  Serial.print(encrypted ? 1 : 0);
  Serial.print(" axisSub=");
  Serial.print(conn.axisSubscribed ? 1 : 0);
  Serial.print(" btnSub=");
  Serial.print(conn.buttonSubscribed ? 1 : 0);
  Serial.print(" source=");
  Serial.print(sourceName());
  Serial.print(" led=");
  Serial.println(conn.led);
  Serial.print("axes ");
  Serial.print(axes.x);
  Serial.print(' ');
  Serial.print(axes.y);
  Serial.print(' ');
  Serial.print(axes.z);
  Serial.print(' ');
  Serial.print(axes.rx);
  Serial.print(' ');
  Serial.print(axes.ry);
  Serial.print(' ');
  Serial.println(axes.rz);
  Serial.print("buttons 0x");
  Serial.println(buttons, HEX);
}

void sendAxes() {
  if (!conn.connected || !conn.encrypted || !conn.axisSubscribed || axisReport == nullptr) {
    return;
  }
  axisReport->setValue(reinterpret_cast<uint8_t*>(&axes), sizeof(axes));
  axisReport->notify();
}

void sendButtons() {
  if (!conn.connected || !conn.encrypted || !conn.buttonSubscribed || buttonReport == nullptr) {
    return;
  }
  const uint8_t raw[4] = {
      static_cast<uint8_t>(buttons),
      static_cast<uint8_t>(buttons >> 8),
      static_cast<uint8_t>(buttons >> 16),
      static_cast<uint8_t>(buttons >> 24),
  };
  buttonReport->setValue(raw, sizeof(raw));
  buttonReport->notify();
}

void sendBattery() {
  if (hid == nullptr) {
    return;
  }
  hid->setBatteryLevel(battery);
  if (conn.connected && conn.encrypted) {
    hid->batteryLevel()->notify();
  }
}

void zeroInput() {
  axes = {};
  buttons = 0;
  sendAxes();
  sendButtons();
}

void updateDemo(uint32_t now) {
  constexpr uint32_t kPeriodMs = 2000;
  const int axis = (now / kPeriodMs) % 6;
  const uint32_t t = now % kPeriodMs;
  const int16_t value = (t < kPeriodMs / 2)
                            ? map(t, 0, kPeriodMs / 2, kAxisMin, kAxisMax)
                            : map(t, kPeriodMs / 2, kPeriodMs, kAxisMax, kAxisMin);
  int16_t* fields[6] = {&axes.x, &axes.y, &axes.z, &axes.rx, &axes.ry, &axes.rz};
  axes = {};
  *fields[axis] = value;
}

void handleCommand(const char* text) {
  text = skipSpace(text);
  if (*text == '\0') {
    return;
  }

  const char cmd = toupper(static_cast<unsigned char>(*text));
  const char* args = skipSpace(text + 1);

  if (cmd == 'A') {
    long value[6];
    const char* cursor = args;
    for (int i = 0; i < 6; ++i) {
      if (!nextInt(cursor, value[i])) {
        printUsage();
        return;
      }
    }
    axes.x = clampAxis(value[0]);
    axes.y = clampAxis(value[1]);
    axes.z = clampAxis(value[2]);
    axes.rx = clampAxis(value[3]);
    axes.ry = clampAxis(value[4]);
    axes.rz = clampAxis(value[5]);
    source = InputSource::kManual;
    sendAxes();
    printState();
    return;
  }

  if (cmd == 'B') {
    long value = 0;
    const char* cursor = args;
    if (!nextInt(cursor, value)) {
      printUsage();
      return;
    }
    buttons = static_cast<uint32_t>(value);
    sendButtons();
    printState();
    return;
  }

  if (cmd == 'L') {
    long value = 0;
    const char* cursor = args;
    if (!nextInt(cursor, value)) {
      printUsage();
      return;
    }
    if (value < 0) {
      value = 0;
    }
    if (value > 100) {
      value = 100;
    }
    battery = static_cast<uint8_t>(value);
    sendBattery();
    Serial.print("battery ");
    Serial.println(battery);
    return;
  }

  if (cmd == 'Z' && *args == '\0') {
    source = InputSource::kJoystick;
    zeroInput();
    printState();
    return;
  }

  if (cmd == 'D' && *args == '\0') {
    const bool demo = source != InputSource::kDemo;
    source = demo ? InputSource::kDemo : InputSource::kJoystick;
    if (!demo) {
      zeroInput();
    }
    Serial.println(demo ? "demo on" : "demo off");
    return;
  }

  if (cmd == 'J' && *args == '\0') {
    calibrateJoystick();
    return;
  }

  if (cmd == 'V' && *args == '\0') {
    joyLog = !joyLog;
    Serial.println(joyLog ? "joy log on" : "joy log off");
    return;
  }

  if (cmd == 'P' && *args == '\0') {
    printState();
    return;
  }

  printUsage();
}

void finishLine() {
  line[lineLen] = '\0';
  handleCommand(line);
  lineLen = 0;
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r' || ch == '\n') {
      finishLine();
      continue;
    }
    if (lineLen + 1 >= sizeof(line)) {
      continue;
    }
    line[lineLen++] = ch;
    line[lineLen] = '\0';
    if (lineLen == 1) {
      const char cmd = toupper(static_cast<unsigned char>(line[0]));
      if (cmd == 'P' || cmd == 'D' || cmd == 'Z' || cmd == 'J' || cmd == 'V') {
        finishLine();
      }
    }
  }
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, ble_gap_conn_desc* desc) override {
    conn.connected = true;
    conn.encrypted = desc->sec_state.encrypted;
    server->updateConnParams(desc->conn_handle, 8, 12, 0, 200);
    NimBLEDevice::startSecurity(desc->conn_handle);
  }

  void onDisconnect(NimBLEServer* server, ble_gap_conn_desc* desc) override {
    (void)server;
    (void)desc;
    conn.connected = false;
    conn.encrypted = false;
    conn.axisSubscribed = false;
    conn.buttonSubscribed = false;
  }

  void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
    conn.encrypted = desc->sec_state.encrypted;
    conn.connected = true;
  }
};

class ReportCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* characteristic, ble_gap_conn_desc* desc, uint16_t subValue) override {
    (void)desc;
    const bool notify = (subValue & 0x0001) != 0;
    if (characteristic == axisReport) {
      conn.axisSubscribed = notify;
    } else if (characteristic == buttonReport) {
      conn.buttonSubscribed = notify;
    }
  }
};

class LedCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic) override {
    const NimBLEAttValue value = characteristic->getValue();
    conn.led = value.size() > 0 ? value[0] : 0;
  }
};

ServerCallbacks serverCallbacks;
ReportCallbacks reportCallbacks;
LedCallbacks ledCallbacks;

void startBle() {
  NimBLEDevice::init(kDeviceName);
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks, false);
  server->advertiseOnDisconnect(true);

  hid = new NimBLEHIDDevice(server);
  hid->manufacturer("3Dconnexion");
  hid->pnp(kVendorIdSource, kVendorId, kProductId, kProductVersion);
  hid->hidInfo(0x00, 0x02);
  hid->reportMap(kReportMap, sizeof(kReportMap));
  hid->setBatteryLevel(battery);

  axisReport = hid->inputReport(kReportAxis);
  buttonReport = hid->inputReport(kReportButtons);
  NimBLECharacteristic* led = hid->outputReport(kReportLed);

  axisReport->setCallbacks(&reportCallbacks);
  buttonReport->setCallbacks(&reportCallbacks);
  led->setCallbacks(&ledCallbacks);

  const uint8_t zeroAxis[sizeof(AxisReport)] = {};
  const uint8_t zeroButtons[4] = {};
  const uint8_t zeroLed = 0;
  axisReport->setValue(zeroAxis, sizeof(zeroAxis));
  buttonReport->setValue(zeroButtons, sizeof(zeroButtons));
  led->setValue(&zeroLed, 1);

  hid->startServices();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setName(kDeviceName);
  advertising->setAppearance(GENERIC_HID);
  advertising->addServiceUUID(hid->hidService()->getUUID());
  advertising->setScanResponse(true);
  advertising->setMinInterval(32);
  advertising->setMaxInterval(48);
  advertising->setMinPreferred(8);
  advertising->setMaxPreferred(12);
  advertising->start();
}

}  // namespace

void setup() {
  pinMode(kLedPin, OUTPUT);
  setBoardLed(false);
  pinMode(kJoyPinX, INPUT);
  pinMode(kJoyPinY, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(kJoyPinX, ADC_11db);
  analogSetPinAttenuation(kJoyPinY, ADC_11db);
  Serial.begin(115200);
  delay(300);
  calibrateJoystick();
  startBle();
  Serial.println(kDeviceName);
  Serial.println("VID 256F PID C638");
  Serial.println("joystick pan: X=A4->X Y=A3->Z");
  printUsage();
}

void loop() {
  pollSerial();

  const uint32_t now = millis();
  setBoardLed(conn.connected ? true : ((now / 500) % 2) == 0);

  static uint32_t lastReportMs = 0;
  if (now - lastReportMs < kReportIntervalMs) {
    return;
  }
  lastReportMs = now;

  if (source == InputSource::kDemo) {
    updateDemo(now);
  } else if (source == InputSource::kJoystick) {
    updateJoystick(now);
  }

  static bool lastZero = false;
  const bool zero = isZero(axes);
  if (!(zero && lastZero)) {
    sendAxes();
  }
  lastZero = zero;
}
