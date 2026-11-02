#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_W     128
#define OLED_H     64

// Настройка пинов под 6-пиновый OLED
#define OLED_MOSI  12   // D1 / SDA
#define OLED_CLK   13   // D0 / SCL
#define OLED_DC    11   // DC / A0
#define OLED_RESET -1   // RES / RST (Обязательно указать этот пин!)
#define OLED_CS    10   // CS отсутствует (-1)

// Конструктор: (ширина, высота, MOSI, CLK, DC, RESET, CS)
Adafruit_SSD1306 display(OLED_W, OLED_H, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ТЕСТ OLED ДИСПЛЕЯ ===");

  // Инициализация экрана
  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println("ОШИБКА: SSD1306 не отвечает! Проверьте провода.");
    while (1) {
      // Остановка при ошибке
    }
  }

  Serial.println("УСПЕХ: Дисплей найден!");

  // Выводим тестовый текст
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(15, 25);
  display.println("OLED OK!");
  display.display();
}

void loop() {
  // Мигаем текстом каждую секунду для проверки работы
  delay(1000);
  display.clearDisplay();
  display.display();

  delay(500);
  display.setCursor(15, 25);
  display.println("OLED OK!");
  display.display();
}
