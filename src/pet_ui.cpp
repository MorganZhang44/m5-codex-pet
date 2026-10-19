#include "pet_ui.h"
#include <M5Unified.h>

namespace {
PetState currentState = PetState::SLEEP;
bool connected = false;

constexpr uint16_t kBackground = TFT_BLACK;
constexpr uint16_t kMuted = 0x8410;

void renderConnection() {
  auto& lcd = M5.Display;
  lcd.fillRect(0, 222, lcd.width(), 10, kBackground);
  lcd.setTextColor(connected ? kMuted : TFT_YELLOW, kBackground);
  lcd.drawCenterString(connected ? "STATUS ONLINE" : "STATUS OFFLINE",
                       lcd.width() / 2, 222);
}

void drawUsageRow(const char* label, int percent, int y) {
  auto& lcd = M5.Display;
  const int barWidth = lcd.width() - 16;
  lcd.setTextColor(TFT_WHITE, kBackground);
  lcd.setCursor(8, y);
  lcd.printf("%s      %d%%", label, percent);
  lcd.drawRect(8, y + 13, barWidth, 9, kMuted);
  lcd.fillRect(9, y + 14, (barWidth - 2) * percent / 100, 7, TFT_CYAN);
}

void renderPet(PetState state) {
  auto& lcd = M5.Display;
  const int cx = lcd.width() / 2;
  const int top = 101;
  const char* label = "RUNNING";
  uint16_t color = TFT_CYAN;
  if (state == PetState::SLEEP) {
    label = "SLEEP";
    color = kMuted;
  } else if (state == PetState::SUCCESS) {
    label = "SUCCESS";
    color = TFT_GREEN;
  } else if (state == PetState::FAILED) {
    label = "FAILED";
    color = TFT_RED;
  }

  // Redraw only the lower section when the state changes.
  lcd.fillRect(0, top, lcd.width(), lcd.height() - top, kBackground);
  lcd.setTextColor(TFT_WHITE, kBackground);
  lcd.drawCenterString("CODEX PET", cx, 110);
  lcd.drawRoundRect(cx - 35, 132, 70, 52, 8, color);
  lcd.drawLine(cx, 124, cx, 132, color);
  lcd.fillCircle(cx, 122, 3, color);

  if (state == PetState::FAILED) {
    for (int eyeX : {cx - 15, cx + 15}) {
      lcd.drawLine(eyeX - 4, 143, eyeX + 4, 151, color);
      lcd.drawLine(eyeX - 4, 151, eyeX + 4, 143, color);
    }
    lcd.drawLine(cx - 10, 173, cx, 168, color);
    lcd.drawLine(cx, 168, cx + 10, 173, color);
  } else if (state == PetState::SLEEP) {
    lcd.drawLine(cx - 20, 149, cx - 10, 149, color);
    lcd.drawLine(cx + 10, 149, cx + 20, 149, color);
    lcd.drawCircle(cx, 169, 3, color);
    lcd.setTextColor(color, kBackground);
    lcd.drawString("zZ", cx + 20, 120);
  } else if (state == PetState::SUCCESS) {
    for (int eyeX : {cx - 15, cx + 15}) {
      lcd.drawLine(eyeX - 5, 150, eyeX, 144, color);
      lcd.drawLine(eyeX, 144, eyeX + 5, 150, color);
    }
    lcd.drawLine(cx - 12, 164, cx, 173, color);
    lcd.drawLine(cx, 173, cx + 12, 164, color);
  } else {
    lcd.fillCircle(cx - 15, 147, 3, color);
    lcd.fillCircle(cx + 15, 147, 3, color);
    lcd.drawRoundRect(cx - 9, 163, 18, 10, 3, color);
    // Static activity rays distinguish RUNNING even without its cyan color.
    lcd.drawLine(cx - 44, 142, cx - 39, 145, color);
    lcd.drawLine(cx + 39, 145, cx + 44, 142, color);
  }
  lcd.setTextColor(color, kBackground);
  lcd.drawCenterString(label, cx, 195);
  renderConnection();
}
}  // namespace

void PetUi::begin() {
  M5.Display.setRotation(0);  // Portrait: 135 x 240.
  M5.Display.setBrightness(128);
  M5.Display.setTextFont(1);
  M5.Display.setTextSize(1);
  M5.Display.fillScreen(kBackground);
  M5.Display.setTextColor(TFT_WHITE, kBackground);
  M5.Display.drawCenterString("USAGE (MOCK)", M5.Display.width() / 2, 8);
  drawUsageRow("5h", 42, 28);
  drawUsageRow("7d", 68, 61);
  M5.Display.drawFastHLine(8, 96, M5.Display.width() - 16, kMuted);
  currentState = PetState::SLEEP;
  connected = false;
  renderPet(currentState);
}

void PetUi::setState(PetState state) {
  if (state == currentState) return;
  currentState = state;
  renderPet(state);
}

void PetUi::setConnected(bool value) {
  if (connected == value) return;
  connected = value;
  renderConnection();
}
