#include "board.h"

#include <Wire.h>

namespace {

constexpr int PIN_I2C_SDA = 8;
constexpr int PIN_I2C_SCL = 9;
constexpr int PIN_TOUCH_INT = 4;

// CH422G IO expander: one I2C address per register
constexpr uint8_t CH422G_MODE = 0x24;    // write 0x01 = IO0-7 are outputs
constexpr uint8_t CH422G_OUTPUT = 0x38;  // output levels of IO0-7

// CH422G output bits
constexpr uint8_t EXIO_TOUCH_RST = 1 << 1;
constexpr uint8_t EXIO_BACKLIGHT = 1 << 2;
constexpr uint8_t EXIO_LCD_RST = 1 << 3;
constexpr uint8_t EXIO_SD_CS = 1 << 4;
constexpr uint8_t EXIO_USB_SEL = 1 << 5;

// GT911 touch controller
constexpr uint8_t GT911_ADDR = 0x5D;
constexpr uint16_t GT911_STATUS = 0x814E;
constexpr uint16_t GT911_POINT1 = 0x8150;

uint8_t exioState = EXIO_LCD_RST | EXIO_BACKLIGHT | EXIO_USB_SEL | EXIO_SD_CS;

void writeExpander(uint8_t addr, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(value);
  Wire.endTransmission();
}

bool gt911Read(uint16_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)GT911_ADDR, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

void gt911Write(uint16_t reg, uint8_t value) {
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(value);
  Wire.endTransmission();
}

TouchPoint lastTouch;

}  // namespace

namespace board {

void begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

  writeExpander(CH422G_MODE, 0x01);

  // Touch reset: hold INT low while releasing reset selects I2C address 0x5D
  exioState &= ~EXIO_TOUCH_RST;
  writeExpander(CH422G_OUTPUT, exioState);
  pinMode(PIN_TOUCH_INT, OUTPUT);
  digitalWrite(PIN_TOUCH_INT, LOW);
  delay(100);
  exioState |= EXIO_TOUCH_RST;
  writeExpander(CH422G_OUTPUT, exioState);
  delay(200);
  pinMode(PIN_TOUCH_INT, INPUT);
}

void setBacklight(bool on) {
  if (on)
    exioState |= EXIO_BACKLIGHT;
  else
    exioState &= ~EXIO_BACKLIGHT;
  writeExpander(CH422G_OUTPUT, exioState);
}

TouchPoint readTouch() {
  uint8_t status = 0;
  if (!gt911Read(GT911_STATUS, &status, 1)) return lastTouch;

  // Bit 7 = new data ready. If not ready, keep the previous state.
  if (!(status & 0x80)) return lastTouch;

  uint8_t count = status & 0x0F;
  TouchPoint tp;
  if (count > 0 && count <= 5) {
    uint8_t data[6];
    if (gt911Read(GT911_POINT1, data, sizeof(data))) {
      tp.pressed = true;
      tp.x = data[1] | (data[2] << 8);
      tp.y = data[3] | (data[4] << 8);
    }
  }
  gt911Write(GT911_STATUS, 0);  // acknowledge
  lastTouch = tp;
  return tp;
}

}  // namespace board
