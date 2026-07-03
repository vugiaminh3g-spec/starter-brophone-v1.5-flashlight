#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_FT6206.h>

// -------------------- Pins --------------------

#define TFT_SCK   12
#define TFT_MISO  13
#define TFT_MOSI  11

#define TFT_CS    15
#define TFT_DC    2

#define TOUCH_SDA 10
#define TOUCH_SCL 8

#define BAT_PIN   1

#define TFT_BL    6
#define FLASH_PIN 7

// -------------------- Objects --------------------

Adafruit_ILI9341 tft(TFT_CS, TFT_DC);
Adafruit_FT6206 touch = Adafruit_FT6206();

// -------------------- Screens --------------------

enum Screen {
  HOME,
  PHONE,
  CALCULATOR,
  PAINT,
  SETTINGS,
  ABOUT
};

Screen currentScreen = HOME;

// -------------------- Paint --------------------

uint16_t paintColor = ILI9341_GREEN;
int brushSize = 3;

// -------------------- Battery --------------------

unsigned long lastBatteryUpdate = 0;
float lastBatteryVoltage = 0;
int lastBatteryPercent = -1;

bool lowBatteryPopup = false;
bool lowBatteryAcknowledged = false;

const int LOW_BATTERY_PERCENT = 10;

// -------------------- Touch --------------------

int lastTouchX = -1;
int lastTouchY = -1;

// -------------------- Settings --------------------

bool wifiSetting = true;
bool soundSetting = true;
bool performanceSetting = true;
bool flashlightOn = false;

int brightnessSetting = 2; // 0 low, 1 medium, 2 high

// -------------------- Calculator --------------------

String calcDisplay = "0";
double calcStored = 0;
char calcOp = 0;
bool calcWaitingNew = false;
bool calcError = false;

// ------------------------------------------------------
// Brightness + flashlight
// ------------------------------------------------------

void applyBrightness() {
  int pwm = 255;

  if (brightnessSetting == 0) pwm = 35;
  if (brightnessSetting == 1) pwm = 130;
  if (brightnessSetting == 2) pwm = 255;

  analogWrite(TFT_BL, pwm);
}

void setFlashlight(bool on) {
  flashlightOn = on;
  digitalWrite(FLASH_PIN, flashlightOn ? HIGH : LOW);
}

// ------------------------------------------------------
// Battery
// ------------------------------------------------------

float readBatteryVoltage() {
  int raw = analogRead(BAT_PIN);

  // Wokwi potentiometer simulates Li-ion battery from 3.00V to 4.20V
  float batteryVoltage = 3.00 + (raw / 4095.0) * 1.20;

  return batteryVoltage;
}

int interpBattery(float v, float v1, float v2, int p1, int p2) {
  float ratio = (v - v1) / (v2 - v1);
  int p = p1 + ratio * (p2 - p1);
  return constrain(p, 0, 100);
}

int batteryPercent(float v) {
  if (v >= 4.20) return 100;
  if (v <= 3.00) return 0;

  if (v >= 4.10) return interpBattery(v, 4.10, 4.20, 90, 100);
  if (v >= 4.00) return interpBattery(v, 4.00, 4.10, 80, 90);
  if (v >= 3.90) return interpBattery(v, 3.90, 4.00, 65, 80);
  if (v >= 3.80) return interpBattery(v, 3.80, 3.90, 50, 65);
  if (v >= 3.70) return interpBattery(v, 3.70, 3.80, 35, 50);
  if (v >= 3.60) return interpBattery(v, 3.60, 3.70, 20, 35);
  if (v >= 3.45) return interpBattery(v, 3.45, 3.60, 10, 20);
  if (v >= 3.30) return interpBattery(v, 3.30, 3.45, 5, 10);

  return interpBattery(v, 3.00, 3.30, 0, 5);
}

void drawBattery(int percent, float volts) {
  int x = 170;
  int y = 8;
  int w = 42;
  int h = 16;

  tft.fillRect(118, 0, 122, 32, ILI9341_BLACK);

  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(120, 11);
  tft.print(volts, 2);
  tft.print("V");

  tft.drawRect(x, y, w, h, ILI9341_WHITE);
  tft.fillRect(x + w, y + 5, 4, 6, ILI9341_WHITE);

  tft.fillRect(x + 2, y + 2, w - 4, h - 4, ILI9341_BLACK);

  int fillW = map(percent, 0, 100, 0, w - 4);

  uint16_t color = ILI9341_GREEN;
  if (percent < 30) color = ILI9341_YELLOW;
  if (percent < 15) color = ILI9341_RED;

  tft.fillRect(x + 2, y + 2, fillW, h - 4, color);

  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(215, 11);
  tft.print(percent);
  tft.print("%");
}

void drawLowBatteryWarning(int percent, float volts) {
  lowBatteryPopup = true;

  tft.fillRoundRect(20, 95, 200, 130, 12, ILI9341_RED);
  tft.drawRoundRect(20, 95, 200, 130, 12, ILI9341_WHITE);

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(42, 112);
  tft.print("LOW BATTERY");

  tft.setTextSize(1);
  tft.setCursor(50, 145);
  tft.print("Battery almost empty!");

  tft.setCursor(68, 165);
  tft.print(percent);
  tft.print("%  ");
  tft.print(volts, 2);
  tft.print("V");

  tft.fillRoundRect(70, 190, 100, 24, 6, ILI9341_BLACK);
  tft.drawRoundRect(70, 190, 100, 24, 6, ILI9341_WHITE);

  tft.setTextSize(2);
  tft.setCursor(108, 194);
  tft.print("OK");
}

void drawCurrentScreenAgain();

void updateBatteryIfNeeded() {
  if (millis() - lastBatteryUpdate < 600) return;
  lastBatteryUpdate = millis();

  float v = readBatteryVoltage();
  int p = batteryPercent(v);

  lastBatteryVoltage = v;
  lastBatteryPercent = p;

  if (!lowBatteryPopup) {
    drawBattery(p, v);
  }

  if (p <= LOW_BATTERY_PERCENT && !lowBatteryAcknowledged && !lowBatteryPopup) {
    drawLowBatteryWarning(p, v);
  }

  if (p > LOW_BATTERY_PERCENT + 5) {
    lowBatteryAcknowledged = false;
  }
}

// ------------------------------------------------------
// UI helpers
// ------------------------------------------------------

void drawWallpaper() {
  tft.fillScreen(ILI9341_BLACK);

  for (int y = 32; y < 320; y += 8) {
    uint16_t color = tft.color565(0, 0, y / 2);
    tft.drawFastHLine(0, y, 240, color);
  }

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_DARKGREY);
  tft.setCursor(42, 155);
  tft.print("BroPhone");
}

void drawStatusBar() {
  tft.fillRect(0, 0, 240, 32, ILI9341_BLACK);
  tft.drawFastHLine(0, 31, 240, ILI9341_DARKGREY);

  tft.setTextSize(1);
  tft.setTextColor(ILI9341_CYAN);
  tft.setCursor(8, 11);
  tft.print("BroPhone");

  if (flashlightOn) {
    tft.setTextColor(ILI9341_YELLOW);
    tft.setCursor(72, 11);
    tft.print("FLASH");
  }

  float v = readBatteryVoltage();
  int p = batteryPercent(v);

  lastBatteryVoltage = v;
  lastBatteryPercent = p;

  drawBattery(p, v);
}

void drawBackButton() {
  tft.fillRoundRect(6, 286, 70, 28, 6, ILI9341_DARKGREY);
  tft.drawRoundRect(6, 286, 70, 28, 6, ILI9341_WHITE);

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(18, 292);
  tft.print("Back");
}

void drawIcon(int x, int y, uint16_t color, const char *bigText, const char *label) {
  tft.fillRoundRect(x, y, 82, 72, 12, color);
  tft.drawRoundRect(x, y, 82, 72, 12, ILI9341_WHITE);

  tft.setTextColor(ILI9341_BLACK);
  tft.setTextSize(3);
  tft.setCursor(x + 25, y + 10);
  tft.print(bigText);

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(x + 14, y + 55);
  tft.print(label);
}

void drawSettingRow(int y, const char *name, const char *value, uint16_t color) {
  tft.fillRoundRect(15, y, 210, 32, 6, color);
  tft.drawRoundRect(15, y, 210, 32, 6, ILI9341_DARKGREY);

  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(25, y + 12);
  tft.print(name);

  tft.setCursor(155, y + 12);
  tft.print(value);
}

bool touchedButton(int x, int y, int bx, int by, int bw, int bh) {
  return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

bool readTouch(int &x, int &y) {
  if (!touch.touched()) return false;

  TS_Point p = touch.getPoint();

  // Last working Wokwi coordinate code
  x = map(p.x, 0, 240, 240, 0);
  y = map(p.y, 0, 320, 320, 0);

  x = constrain(x, 0, 239);
  y = constrain(y, 0, 319);

  return true;
}

// ------------------------------------------------------
// Calculator logic
// ------------------------------------------------------

String formatNumber(double v) {
  if (isnan(v) || isinf(v)) return "Error";

  String s = String(v, 6);

  while (s.endsWith("0") && s.indexOf(".") >= 0) {
    s.remove(s.length() - 1);
  }

  if (s.endsWith(".")) {
    s.remove(s.length() - 1);
  }

  if (s.length() > 14) {
    s = String(v, 2);
  }

  return s;
}

void calcReset() {
  calcDisplay = "0";
  calcStored = 0;
  calcOp = 0;
  calcWaitingNew = false;
  calcError = false;
}

double calcCurrentValue() {
  return calcDisplay.toDouble();
}

void calcSetError() {
  calcDisplay = "Error";
  calcStored = 0;
  calcOp = 0;
  calcWaitingNew = true;
  calcError = true;
}

void calcApplyOperation(double current) {
  if (calcOp == '+') calcStored += current;
  else if (calcOp == '-') calcStored -= current;
  else if (calcOp == '*') calcStored *= current;
  else if (calcOp == '/') {
    if (current == 0) {
      calcSetError();
      return;
    }
    calcStored /= current;
  }

  calcDisplay = formatNumber(calcStored);
}

void calcPressDigit(char d) {
  if (calcError) calcReset();

  if (calcWaitingNew || calcDisplay == "0") {
    calcDisplay = String(d);
    calcWaitingNew = false;
  } else {
    if (calcDisplay.length() < 14) {
      calcDisplay += d;
    }
  }
}

void calcPressDot() {
  if (calcError) calcReset();

  if (calcWaitingNew) {
    calcDisplay = "0.";
    calcWaitingNew = false;
    return;
  }

  if (calcDisplay.indexOf(".") < 0) {
    calcDisplay += ".";
  }
}

void calcPressOperator(char op) {
  if (calcError) calcReset();

  double current = calcCurrentValue();

  if (calcOp != 0 && !calcWaitingNew) {
    calcApplyOperation(current);
    if (calcError) return;
  } else {
    calcStored = current;
  }

  calcOp = op;
  calcWaitingNew = true;
}

void calcPressEquals() {
  if (calcError) calcReset();

  if (calcOp == 0) return;

  double current = calcCurrentValue();
  calcApplyOperation(current);

  calcOp = 0;
  calcWaitingNew = true;
}

void calcPressDelete() {
  if (calcError) {
    calcReset();
    return;
  }

  if (calcWaitingNew) return;

  if (calcDisplay.length() > 1) {
    calcDisplay.remove(calcDisplay.length() - 1);
  } else {
    calcDisplay = "0";
  }
}

void calcPressSign() {
  if (calcError) calcReset();

  if (calcDisplay == "0") return;

  if (calcDisplay.startsWith("-")) {
    calcDisplay.remove(0, 1);
  } else {
    calcDisplay = "-" + calcDisplay;
  }
}

void drawCalcDisplay() {
  tft.fillRoundRect(10, 45, 220, 42, 8, ILI9341_BLACK);
  tft.drawRoundRect(10, 45, 220, 42, 8, ILI9341_WHITE);

  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(2);

  int textX = 20;
  if (calcDisplay.length() <= 8) textX = 220 - calcDisplay.length() * 12;
  if (textX < 20) textX = 20;

  tft.setCursor(textX, 58);
  tft.print(calcDisplay);

  if (calcOp != 0) {
    tft.setTextColor(ILI9341_YELLOW);
    tft.setTextSize(1);
    tft.setCursor(18, 76);
    tft.print(calcOp);
  }
}

void drawCalcButton(int x, int y, int w, int h, const char *label, uint16_t color) {
  tft.fillRoundRect(x, y, w, h, 7, color);
  tft.drawRoundRect(x, y, w, h, 7, ILI9341_WHITE);

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);

  int tx = x + 17;
  if (strlen(label) == 2) tx = x + 12;
  if (strlen(label) == 3) tx = x + 7;

  tft.setCursor(tx, y + 8);
  tft.print(label);
}

void drawCalculator() {
  currentScreen = CALCULATOR;
  lowBatteryPopup = false;

  tft.fillScreen(ILI9341_BLACK);
  drawStatusBar();

  // Small title, no collision with display
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_GREEN);
  tft.setCursor(90, 35);
  tft.print("Calculator");

  // Display box
  tft.fillRoundRect(10, 48, 220, 42, 8, ILI9341_BLACK);
  tft.drawRoundRect(10, 48, 220, 42, 8, ILI9341_WHITE);
  drawCalcDisplay();

  int x0 = 10;
  int y0 = 98;
  int bw = 50;
  int bh = 32;
  int gx = 5;
  int gy = 6;

  drawCalcButton(x0 + 0 * (bw + gx), y0 + 0 * (bh + gy), bw, bh, "C", ILI9341_RED);
  drawCalcButton(x0 + 1 * (bw + gx), y0 + 0 * (bh + gy), bw, bh, "Del", ILI9341_ORANGE);
  drawCalcButton(x0 + 2 * (bw + gx), y0 + 0 * (bh + gy), bw, bh, "/", ILI9341_DARKGREY);
  drawCalcButton(x0 + 3 * (bw + gx), y0 + 0 * (bh + gy), bw, bh, "*", ILI9341_DARKGREY);

  drawCalcButton(x0 + 0 * (bw + gx), y0 + 1 * (bh + gy), bw, bh, "7", ILI9341_BLUE);
  drawCalcButton(x0 + 1 * (bw + gx), y0 + 1 * (bh + gy), bw, bh, "8", ILI9341_BLUE);
  drawCalcButton(x0 + 2 * (bw + gx), y0 + 1 * (bh + gy), bw, bh, "9", ILI9341_BLUE);
  drawCalcButton(x0 + 3 * (bw + gx), y0 + 1 * (bh + gy), bw, bh, "-", ILI9341_DARKGREY);

  drawCalcButton(x0 + 0 * (bw + gx), y0 + 2 * (bh + gy), bw, bh, "4", ILI9341_BLUE);
  drawCalcButton(x0 + 1 * (bw + gx), y0 + 2 * (bh + gy), bw, bh, "5", ILI9341_BLUE);
  drawCalcButton(x0 + 2 * (bw + gx), y0 + 2 * (bh + gy), bw, bh, "6", ILI9341_BLUE);
  drawCalcButton(x0 + 3 * (bw + gx), y0 + 2 * (bh + gy), bw, bh, "+", ILI9341_DARKGREY);

  drawCalcButton(x0 + 0 * (bw + gx), y0 + 3 * (bh + gy), bw, bh, "1", ILI9341_BLUE);
  drawCalcButton(x0 + 1 * (bw + gx), y0 + 3 * (bh + gy), bw, bh, "2", ILI9341_BLUE);
  drawCalcButton(x0 + 2 * (bw + gx), y0 + 3 * (bh + gy), bw, bh, "3", ILI9341_BLUE);
  drawCalcButton(x0 + 3 * (bw + gx), y0 + 3 * (bh + gy), bw, bh, "=", ILI9341_GREEN);

  drawCalcButton(x0 + 0 * (bw + gx), y0 + 4 * (bh + gy), bw, bh, "+/-", ILI9341_BLUE);
  drawCalcButton(x0 + 1 * (bw + gx), y0 + 4 * (bh + gy), bw, bh, "0", ILI9341_BLUE);
  drawCalcButton(x0 + 2 * (bw + gx), y0 + 4 * (bh + gy), bw, bh, ".", ILI9341_BLUE);

  drawBackButton();
}

void handleCalcButton(const char *label) {
  if (strcmp(label, "C") == 0) calcReset();
  else if (strcmp(label, "Del") == 0) calcPressDelete();
  else if (strcmp(label, "+/-") == 0) calcPressSign();
  else if (strcmp(label, ".") == 0) calcPressDot();
  else if (strcmp(label, "+") == 0) calcPressOperator('+');
  else if (strcmp(label, "-") == 0) calcPressOperator('-');
  else if (strcmp(label, "*") == 0) calcPressOperator('*');
  else if (strcmp(label, "/") == 0) calcPressOperator('/');
  else if (strcmp(label, "=") == 0) calcPressEquals();
  else if (strlen(label) == 1 && label[0] >= '0' && label[0] <= '9') calcPressDigit(label[0]);

  drawCalcDisplay();
}

void handleCalculatorTouch(int x, int y) {
  if (touchedButton(x, y, 6, 286, 70, 28)) {
    drawHome();
    return;
  }

  int x0 = 10;
  int y0 = 98;
  int bw = 50;
  int bh = 32;
  int gx = 5;
  int gy = 6;

  const char *labels[5][4] = {
    {"C",   "Del", "/", "*"},
    {"7",   "8",   "9", "-"},
    {"4",   "5",   "6", "+"},
    {"1",   "2",   "3", "="},
    {"+/-", "0",   ".", ""}
  };

  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 4; col++) {
      int bx = x0 + col * (bw + gx);
      int by = y0 + row * (bh + gy);

      if (touchedButton(x, y, bx, by, bw, bh)) {
        if (strlen(labels[row][col]) > 0) {
          handleCalcButton(labels[row][col]);
        }
        return;
      }
    }
  }
}

// ------------------------------------------------------
// Screens
// ------------------------------------------------------

void drawHome() {
  currentScreen = HOME;
  lowBatteryPopup = false;

  drawWallpaper();
  drawStatusBar();

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(42, 42);
  tft.print("BroPhone OS");

  drawIcon(20, 80, ILI9341_GREEN, "P", "Phone");
  drawIcon(138, 80, ILI9341_CYAN, "=", "Calc");
  drawIcon(20, 178, ILI9341_MAGENTA, "D", "Paint");
  drawIcon(138, 178, ILI9341_YELLOW, "S", "Settings");
}

void drawPhone() {
  currentScreen = PHONE;
  lowBatteryPopup = false;

  tft.fillScreen(ILI9341_BLACK);
  drawStatusBar();

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_GREEN);
  tft.setCursor(25, 48);
  tft.print("Phone App");

  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(20, 85);
  tft.print("Fake Call Ready");

  int startX = 30;
  int startY = 120;
  int n = 1;

  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      int bx = startX + col * 60;
      int by = startY + row * 42;

      tft.fillRoundRect(bx, by, 44, 32, 6, ILI9341_DARKGREY);
      tft.drawRoundRect(bx, by, 44, 32, 6, ILI9341_WHITE);

      tft.setTextSize(2);
      tft.setTextColor(ILI9341_WHITE);
      tft.setCursor(bx + 16, by + 8);
      tft.print(n);
      n++;
    }
  }

  tft.fillRoundRect(90, 248, 60, 30, 8, ILI9341_GREEN);
  tft.setTextColor(ILI9341_BLACK);
  tft.setCursor(105, 255);
  tft.print("Call");

  drawBackButton();
}

void drawPaintUI() {
  currentScreen = PAINT;
  lowBatteryPopup = false;

  tft.fillScreen(ILI9341_BLACK);
  drawStatusBar();

  tft.fillRect(0, 33, 40, 34, ILI9341_RED);
  tft.fillRect(40, 33, 40, 34, ILI9341_YELLOW);
  tft.fillRect(80, 33, 40, 34, ILI9341_GREEN);
  tft.fillRect(120, 33, 40, 34, ILI9341_CYAN);
  tft.fillRect(160, 33, 40, 34, ILI9341_BLUE);
  tft.fillRect(200, 33, 40, 34, ILI9341_MAGENTA);

  tft.drawRect(0, 70, 240, 210, ILI9341_DARKGREY);

  drawBackButton();

  tft.fillRoundRect(160, 286, 72, 28, 6, ILI9341_RED);
  tft.drawRoundRect(160, 286, 72, 28, 6, ILI9341_WHITE);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(170, 292);
  tft.print("Clear");

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_DARKGREY);
  tft.setCursor(55, 165);
  tft.print("Draw");
}

void drawSettings() {
  currentScreen = SETTINGS;
  lowBatteryPopup = false;

  tft.fillScreen(ILI9341_BLACK);
  drawStatusBar();

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(25, 48);
  tft.print("Settings");

  drawSettingRow(72, "WiFi", wifiSetting ? "ON" : "OFF", ILI9341_DARKGREY);
  drawSettingRow(110, "Sound", soundSetting ? "ON" : "OFF", ILI9341_DARKGREY);
  drawSettingRow(148, "CPU", performanceSetting ? "240MHz" : "80MHz", ILI9341_DARKGREY);

  const char *brightText = "HIGH";
  if (brightnessSetting == 0) brightText = "LOW";
  if (brightnessSetting == 1) brightText = "MED";

  drawSettingRow(186, "Brightness", brightText, ILI9341_DARKGREY);
  drawSettingRow(224, "Flashlight", flashlightOn ? "ON" : "OFF", ILI9341_DARKGREY);
  drawSettingRow(262, "About", "OPEN", ILI9341_BLUE);

  drawBackButton();
}

void drawAboutPage() {
  currentScreen = ABOUT;
  lowBatteryPopup = false;

  tft.fillScreen(ILI9341_BLACK);
  drawStatusBar();

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_CYAN);
  tft.setCursor(35, 48);
  tft.print("About");

  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);

  tft.setCursor(20, 90);
  tft.print("Device: BroPhone v1.3 Calc");

  tft.setCursor(20, 112);
  tft.print("Chip: ESP32-S3");

  tft.setCursor(20, 134);
  tft.print("CPU: ");
  tft.print(getCpuFrequencyMhz());
  tft.print(" MHz");

  tft.setCursor(20, 156);
  tft.print("Display: ILI9341 Touch");

  tft.setCursor(20, 178);
  tft.print("Battery: ");
  tft.print(lastBatteryVoltage, 2);
  tft.print("V ");
  tft.print(lastBatteryPercent);
  tft.print("%");

  tft.setCursor(20, 200);
  tft.print("Chat removed: yes");

  tft.setCursor(20, 222);
  tft.print("Calculator: working");

  drawBackButton();
}

void drawCurrentScreenAgain() {
  if (currentScreen == HOME) drawHome();
  else if (currentScreen == PHONE) drawPhone();
  else if (currentScreen == CALCULATOR) drawCalculator();
  else if (currentScreen == PAINT) drawPaintUI();
  else if (currentScreen == SETTINGS) drawSettings();
  else if (currentScreen == ABOUT) drawAboutPage();
}

// ------------------------------------------------------
// Touch handlers
// ------------------------------------------------------

void handleHomeTouch(int x, int y) {
  if (touchedButton(x, y, 20, 80, 82, 72)) {
    drawPhone();
  } else if (touchedButton(x, y, 138, 80, 82, 72)) {
    drawCalculator();
  } else if (touchedButton(x, y, 20, 178, 82, 72)) {
    drawPaintUI();
  } else if (touchedButton(x, y, 138, 178, 82, 72)) {
    drawSettings();
  }
}

void handlePaintTouch(int x, int y) {
  if (y >= 33 && y <= 67) {
    if (x < 40) paintColor = ILI9341_RED;
    else if (x < 80) paintColor = ILI9341_YELLOW;
    else if (x < 120) paintColor = ILI9341_GREEN;
    else if (x < 160) paintColor = ILI9341_CYAN;
    else if (x < 200) paintColor = ILI9341_BLUE;
    else paintColor = ILI9341_MAGENTA;
    return;
  }

  if (touchedButton(x, y, 6, 286, 70, 28)) {
    drawHome();
    return;
  }

  if (touchedButton(x, y, 160, 286, 72, 28)) {
    drawPaintUI();
    return;
  }

  if (x >= 0 && x < 240 && y >= 70 && y <= 280) {
    tft.fillCircle(x, y, brushSize, paintColor);
  }
}

void handleSettingsTouch(int x, int y) {
  if (touchedButton(x, y, 6, 286, 70, 28)) {
    drawHome();
    return;
  }

  if (touchedButton(x, y, 15, 72, 210, 32)) {
    wifiSetting = !wifiSetting;
    drawSettings();
    return;
  }

  if (touchedButton(x, y, 15, 110, 210, 32)) {
    soundSetting = !soundSetting;
    drawSettings();
    return;
  }

  if (touchedButton(x, y, 15, 148, 210, 32)) {
    performanceSetting = !performanceSetting;

    if (performanceSetting) {
      setCpuFrequencyMhz(240);
    } else {
      setCpuFrequencyMhz(80);
    }

    drawSettings();
    return;
  }

  if (touchedButton(x, y, 15, 186, 210, 32)) {
    brightnessSetting++;
    if (brightnessSetting > 2) brightnessSetting = 0;

    applyBrightness();
    drawSettings();
    return;
  }

  if (touchedButton(x, y, 15, 224, 210, 32)) {
    setFlashlight(!flashlightOn);
    drawSettings();
    return;
  }

  if (touchedButton(x, y, 15, 262, 210, 32)) {
    drawAboutPage();
    return;
  }
}

void handleAboutTouch(int x, int y) {
  if (touchedButton(x, y, 6, 286, 70, 28)) {
    drawSettings();
  }
}

void handleGenericAppTouch(int x, int y) {
  if (touchedButton(x, y, 6, 286, 70, 28)) {
    drawHome();
  }
}

// ------------------------------------------------------
// Main
// ------------------------------------------------------

void setup() {
  setCpuFrequencyMhz(240);

  Serial.begin(115200);
  delay(100);

  analogReadResolution(12);

  pinMode(TFT_BL, OUTPUT);
  pinMode(FLASH_PIN, OUTPUT);

  setFlashlight(false);
  applyBrightness();

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI);
  Wire.setPins(TOUCH_SDA, TOUCH_SCL);

  tft.begin();
  tft.setSPISpeed(80000000);

  if (!touch.begin(40)) {
    tft.fillScreen(ILI9341_RED);
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(20, 130);
    tft.print("TOUCH ERROR");
    while (true) delay(100);
  }

  Serial.println("BroPhone v1.3 Calculator Edition booted");
  Serial.print("CPU MHz: ");
  Serial.println(getCpuFrequencyMhz());

  calcReset();
  drawHome();
}

void loop() {
  updateBatteryIfNeeded();

  int x, y;
  if (!readTouch(x, y)) {
    delay(5);
    return;
  }

  if (lowBatteryPopup) {
    if (touchedButton(x, y, 70, 190, 100, 24)) {
      lowBatteryPopup = false;
      lowBatteryAcknowledged = true;
      drawCurrentScreenAgain();
    }

    delay(160);
    return;
  }

  if (abs(x - lastTouchX) < 3 && abs(y - lastTouchY) < 3) {
    if (currentScreen != PAINT) {
      delay(80);
      return;
    }
  }

  lastTouchX = x;
  lastTouchY = y;

  if (currentScreen == HOME) {
    handleHomeTouch(x, y);
    delay(160);
  } else if (currentScreen == CALCULATOR) {
    handleCalculatorTouch(x, y);
    delay(120);
  } else if (currentScreen == PAINT) {
    handlePaintTouch(x, y);
    delay(8);
  } else if (currentScreen == SETTINGS) {
    handleSettingsTouch(x, y);
    delay(160);
  } else if (currentScreen == ABOUT) {
    handleAboutTouch(x, y);
    delay(160);
  } else {
    handleGenericAppTouch(x, y);
    delay(160);
  }
}