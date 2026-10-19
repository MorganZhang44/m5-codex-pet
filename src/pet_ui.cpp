#include "pet_ui.h"
#include <M5Unified.h>
#include "page_navigation.h"

namespace {
PetState currentState = PetState::SLEEP;
TransportState transport = TransportState::CONNECTING;
LastResult lastResult = LastResult::NONE;
Usage currentUsage;
DeviceInfo currentDevice;
PageNavigation navigation;
bool setupScreen = false;

constexpr uint16_t kBackground = TFT_BLACK;
constexpr uint16_t kMuted = 0x8410;

void deviceRow(const char* label, const char* value, int y) {
  auto& lcd = M5.Display;
  lcd.fillRect(0, y, lcd.width(), 26, kBackground);
  lcd.setTextColor(kMuted, kBackground);
  lcd.drawString(label, 8, y);
  char fitted[64];
  DeviceText::fit(value, fitted, sizeof(fitted), (lcd.width() - 16) / 6);
  lcd.setTextColor(TFT_WHITE, kBackground);
  lcd.drawString(fitted, 8, y + 13);
}

void renderRssi() {
  char rssi[24] = "--";
  if (currentDevice.hasRssi) snprintf(rssi, sizeof(rssi), "%ld dBm", long(currentDevice.rssi));
  deviceRow("RSSI", rssi, 98);
}

void renderConnection() {
  auto& lcd = M5.Display;
  const char* label = "CONNECTING";
  if (transport == TransportState::ONLINE) label = "ONLINE";
  else if (transport == TransportState::OFFLINE) label = "OFFLINE";
  else if (transport == TransportState::SETUP) label = "SETUP";
  lcd.fillRect(0, 227, lcd.width(), 10, kBackground);
  lcd.setTextColor(transport == TransportState::ONLINE ? kMuted : TFT_YELLOW, kBackground);
  lcd.drawCenterString(label, lcd.width() / 2, 227);
}

void renderDevice() {
  auto& lcd = M5.Display;
  lcd.fillScreen(kBackground);
  lcd.setTextColor(TFT_WHITE, kBackground);
  lcd.drawCenterString("DEVICE", lcd.width() / 2, 8);
  deviceRow("Wi-Fi SSID", currentDevice.ssid, 32);
  deviceRow("DEVICE IP", currentDevice.ipv4, 65);
  renderRssi();
  deviceRow("BRIDGE IP", currentDevice.bridge, 131);
  deviceRow("Firmware", DeviceText::kFirmwareVersion, 164);
  lcd.setTextColor(kMuted, kBackground);
  lcd.drawCenterString("BtnA: PET", lcd.width() / 2, 207);
  renderConnection();
}

void renderHistory() {
  auto& lcd = M5.Display;
  lcd.fillRect(0, 213, lcd.width(), 10, kBackground);
  lcd.setTextColor(kMuted, kBackground);
  lcd.drawCenterString(lastResult == LastResult::SUCCESS ? "LAST OK" :
                       lastResult == LastResult::FAILED ? "LAST FAIL" : "LAST --",
                       lcd.width() / 2, 213);
}

void drawUsageRow(const char* label, const UsageReset& reset, bool fiveHour,
                  double percent, int y) {
  auto& lcd = M5.Display;
  const int barWidth = lcd.width() - 16;
  lcd.setTextColor(TFT_WHITE, kBackground);
  lcd.setCursor(8, y);
  char resetLabel[7];
  reset.label(resetLabel, sizeof(resetLabel), fiveHour);
  lcd.printf("%s %s", label, resetLabel);
  char used[8];
  if (percent < 0) snprintf(used, sizeof(used), "--%%");
  else snprintf(used, sizeof(used), "%.0f%%", percent);
  lcd.drawRightString(used, lcd.width() - 8, y);
  lcd.drawRect(8, y + 13, barWidth, 9, kMuted);
  if (percent >= 0) {
    lcd.fillRect(9, y + 14, static_cast<int>((barWidth - 2) * percent / 100),
                 7, TFT_CYAN);
  }
}

void renderUsage() {
  auto& lcd = M5.Display;
  lcd.fillRect(0, 0, lcd.width(), 96, kBackground);
  lcd.setTextColor(currentUsage.stale ? TFT_YELLOW : TFT_WHITE, kBackground);
  lcd.drawCenterString(currentUsage.stale ? "USAGE STALE" : "USAGE",
                       lcd.width() / 2, 8);
  drawUsageRow("5h", currentUsage.fiveHourReset, true, currentUsage.fiveHour, 28);
  drawUsageRow("7d", currentUsage.sevenDayReset, false, currentUsage.sevenDay, 61);
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
  renderHistory();
  renderConnection();
}
}  // namespace

void PetUi::begin() {
  navigation = PageNavigation{};
  setupScreen = false;
  currentDevice = DeviceInfo{};
  M5.Display.setRotation(0);  // Portrait: 135 x 240.
  M5.Display.setBrightness(128);
  M5.Display.setTextFont(1);
  M5.Display.setTextSize(1);
  M5.Display.fillScreen(kBackground);
  M5.Display.setTextColor(TFT_WHITE, kBackground);
  currentUsage = Usage{};
  renderUsage();
  M5.Display.drawFastHLine(8, 96, M5.Display.width() - 16, kMuted);
  currentState = PetState::SLEEP;
  transport = TransportState::CONNECTING;
  lastResult = LastResult::NONE;
  renderPet(currentState);
}

void PetUi::setState(PetState state) {
  if (state == currentState) return;
  currentState = state;
  if (!setupScreen && !navigation.devicePage()) renderPet(state);
}

void PetUi::setTransport(TransportState value) {
  if (transport == value) return;
  transport = value;
  if (!setupScreen) renderConnection();
}

void PetUi::setLastResult(LastResult result) {
  if (lastResult == result) return;
  lastResult = result;
  if (!setupScreen && !navigation.devicePage()) renderHistory();
}

void PetUi::showSetup() {
  setupScreen = true;
  navigation.enterSetup();
  auto& lcd = M5.Display;
  lcd.fillScreen(kBackground);
  lcd.setTextFont(1);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_YELLOW, kBackground);
  lcd.drawCenterString("USB SETUP", lcd.width() / 2, 10);
  lcd.setTextSize(1);

  lcd.drawString("1 FIRST TIME", 8, 42);
  lcd.setTextColor(TFT_WHITE, kBackground);
  // Font 1 uses 6x8 cells. Wrap the URL and installer path, with extra space between
  // commands so the clone/install flow remains readable on 135x240.
  lcd.drawString("git clone", 8, 64);
  lcd.drawString("https://github.com/", 14, 78);
  lcd.drawString("MorganZhang44/", 14, 90);
  lcd.drawString("m5-codex-pet.git", 14, 102);
  lcd.drawString("./m5-codex-pet/", 8, 126);
  lcd.drawString("install_mac.sh", 8, 138);

  lcd.drawFastHLine(8, 170, lcd.width() - 16, kMuted);
  lcd.setTextColor(TFT_YELLOW, kBackground);
  lcd.drawString("2 SETUP", 8, 184);
  lcd.setTextColor(TFT_WHITE, kBackground);
  lcd.drawString("codex-pet setup", 8, 208);
}

void PetUi::setUsage(const Usage& usage) {
  if (usage == currentUsage) return;
  currentUsage = usage;
  if (!setupScreen && !navigation.devicePage()) renderUsage();
}

void PetUi::setDeviceInfo(const DeviceInfo& info) {
  if (info == currentDevice) return;
  const DeviceInfo previous = currentDevice;
  currentDevice = info;
  if (setupScreen || !navigation.devicePage()) return;
  if (strcmp(previous.ssid, info.ssid) != 0) deviceRow("Wi-Fi SSID", info.ssid, 32);
  if (strcmp(previous.ipv4, info.ipv4) != 0) deviceRow("DEVICE IP", info.ipv4, 65);
  if (previous.hasRssi != info.hasRssi || (info.hasRssi && previous.rssi != info.rssi)) renderRssi();
  if (strcmp(previous.bridge, info.bridge) != 0) deviceRow("BRIDGE IP", info.bridge, 131);
}

void PetUi::updateButton(bool pressed, uint32_t now) {
  if (!navigation.update(pressed, now)) return;
  if (navigation.devicePage()) {
    renderDevice();
  } else {
    M5.Display.fillScreen(kBackground);
    renderUsage();
    M5.Display.drawFastHLine(8, 96, M5.Display.width() - 16, kMuted);
    renderPet(currentState);
  }
}
