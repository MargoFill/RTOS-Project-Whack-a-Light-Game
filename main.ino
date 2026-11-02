#include <Arduino.h>
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/queue.h"
  #include "freertos/semphr.h"

  #include <U8g2lib.h>
  #include <Wire.h>

  #define NUM_ELEMENTS 5

  // === РАСПИНОВКА OLED (I2C) ===
  #define SDA_PIN 12
  #define SCL_PIN 13

  // Конструктор аппаратного I2C, полный буфер (F)
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

  // === РАСПИНОВКА ИГРЫ ===
  const uint8_t ledPins[NUM_ELEMENTS]    = { 4, 5, 6, 7, 15};
  const uint8_t buttonPins[NUM_ELEMENTS] = {16, 17, 18, 8, 9};

  // === УСЛОВИЯ КОНЦА ИГРЫ ===
  const int MAX_MISS  = 5;  // пропустить МОЖНО 5 раз; на 6-м конец
  const int MAX_LIVES = 5;  // 5 неправильных нажатий — конец

  typedef struct {
      int buttonIndex;
  } ButtonEvent_t;

  QueueHandle_t xButtonQueue = NULL;
  SemaphoreHandle_t xSerialMutex = NULL;

  int score = 0;
  int currentTimeoutMs = 1500;
  volatile int  missCount  = 0;
  volatile int  wrongCount = 0;
  volatile bool startGame  = false;
  volatile bool gameOver   = false;

  void safeLog(const char* msg) {
      if (xSemaphoreTake(xSerialMutex, portMAX_DELAY) == pdTRUE) {
          Serial.println(msg);
          xSemaphoreGive(xSerialMutex);
      }
  }

  // === ЗАДАЧА 1: Опрос кнопок (Priority 3) ===
  void TaskButtons(void *pvParameters) {
      bool lastState[NUM_ELEMENTS];
      for (int i = 0; i < NUM_ELEMENTS; i++) {
          pinMode(buttonPins[i], INPUT_PULLUP);
          lastState[i] = HIGH;
      }

      for (;;) {
          for (int i = 0; i < NUM_ELEMENTS; i++) {
              bool currentState = digitalRead(buttonPins[i]);
              if (lastState[i] == HIGH && currentState == LOW) {
                  vTaskDelay(pdMS_TO_TICKS(20));
                  if (digitalRead(buttonPins[i]) == LOW) {
                      ButtonEvent_t event;
                      event.buttonIndex = i;
                      xQueueSend(xButtonQueue, &event, 0);
                  }
              }
              lastState[i] = currentState;
          }
          vTaskDelay(pdMS_TO_TICKS(10));
      }
  }

  // === ЗАДАЧА 2: Логика игры (Priority 2) ===
  void TaskGameLogic(void *pvParameters) {
      ButtonEvent_t rxEvent;

      while (!startGame) {
          vTaskDelay(pdMS_TO_TICKS(50));
      }
      safeLog("=== GAME STARTED ===");

      for (;;) {
          int targetIndex = random(0, NUM_ELEMENTS);
          digitalWrite(ledPins[targetIndex], HIGH);

          char logBuf[80];
          snprintf(logBuf, sizeof(logBuf), "[ROUND] Light LED #%d (%d ms limit)", targetIndex + 1, currentTimeoutMs);
          safeLog(logBuf);

          xQueueReset(xButtonQueue);

          BaseType_t xStatus = xQueueReceive(xButtonQueue, &rxEvent, pdMS_TO_TICKS(currentTimeoutMs));
          digitalWrite(ledPins[targetIndex], LOW);

          if (xStatus == pdTRUE) {
              if (rxEvent.buttonIndex == targetIndex) {
                  score++;
                  snprintf(logBuf, sizeof(logBuf), "-> SUCCESS! Score: %d", score);
                  safeLog(logBuf);
                  if (currentTimeoutMs > 400) currentTimeoutMs -= 25;
              } else {
                  wrongCount++;
                  snprintf(logBuf, sizeof(logBuf), "-> WRONG BUTTON! Pressed #%d. Lives left: %d. Score: %d",
                           rxEvent.buttonIndex + 1, MAX_LIVES - wrongCount, score);
                  safeLog(logBuf);
              }
          } else {
              missCount++;
              snprintf(logBuf, sizeof(logBuf), "-> TIMEOUT! Misses: %d/%d. Score: %d",
                       missCount, MAX_MISS, score);
              safeLog(logBuf);
          }

          // === ПРОВЕРКА КОНЦА ИГРЫ ===
          if (missCount > MAX_MISS || wrongCount >= MAX_LIVES) {
              gameOver = true;
              if (missCount > MAX_MISS)
                  safeLog("=== GAME OVER: too many misses ===");
              else
                  safeLog("=== GAME OVER: out of lives ===");

              for (int i = 0; i < NUM_ELEMENTS; i++) digitalWrite(ledPins[i], LOW);
              vTaskSuspend(NULL);
          }

          vTaskDelay(pdMS_TO_TICKS(1000));
      }
  }

  // === ЗАДАЧА 3: Системный монитор (Priority 1) ===
  void TaskSystemMonitor(void *pvParameters) {
      for (;;) {
          char logBuf[64];
          snprintf(logBuf, sizeof(logBuf), "[SYSTEM] Free Heap: %d bytes", ESP.getFreeHeap());
          safeLog(logBuf);
          vTaskDelay(pdMS_TO_TICKS(10000));
      }
  }

  // === ЗАДАЧА 4: Вывод на OLED (Priority 1) ===
  void TaskDisplay(void *pvParameters) {

      // --- Отсчёт 3 - 2 - 1 ---
      for (int n = 3; n >= 1; n--) {
          u8g2.clearBuffer();
          u8g2.setFont(u8g2_font_fub42_tn);   // крупный цифровой шрифт
          char buf[2]; buf[0] = '0' + n; buf[1] = 0;
          u8g2.drawStr(50, 55, buf);
          u8g2.sendBuffer();
          vTaskDelay(pdMS_TO_TICKS(1000));
      }

      // --- "GO!" ---
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_fub30_tr);
      u8g2.drawStr(25, 48, "GO!");
      u8g2.sendBuffer();
      vTaskDelay(pdMS_TO_TICKS(1000));

      startGame = true;

      // --- Основной цикл экрана ---
      for (;;) {
          u8g2.clearBuffer();

          if (gameOver) {
              // Экран END + финальный счёт
              u8g2.setFont(u8g2_font_fub20_tr);
              u8g2.drawStr(30, 25, "END");
              char buf[24];
              snprintf(buf, sizeof(buf), "Score: %d", score);
              u8g2.setFont(u8g2_font_ncenB10_tr);
              u8g2.drawStr(10, 55, buf);
          } else {
              // Игровой экран: SCORE + число
              u8g2.setFont(u8g2_font_ncenB10_tr);
              u8g2.drawStr(0, 15, "SCORE");
              char buf[12];
              snprintf(buf, sizeof(buf), "%d", score);
              u8g2.setFont(u8g2_font_fub30_tn);
              u8g2.drawStr(0, 62, buf);
          }

          u8g2.sendBuffer();
          vTaskDelay(pdMS_TO_TICKS(gameOver ? 500 : 200));
      }
  }

  void setup() {
      Serial.begin(115200);
      delay(2000);
      Serial.println("\n\n=== SETUP STARTED ===");

      // === Инициализация OLED (I2C) ===
      Wire.begin(SDA_PIN, SCL_PIN);
      u8g2.begin();
      u8g2.setFontPosBottom();

      // КРИТИЧНО: явно стираем старые данные на экране перед новой программой
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_ncenB10_tr);
      u8g2.drawStr(5, 55, "Initializing...");
      u8g2.sendBuffer();
      delay(500);

      Serial.println("OLED (U8g2 I2C) initialized");

      // === Инициализация LED ===
      for (int i = 0; i < NUM_ELEMENTS; i++) {
          pinMode(ledPins[i], OUTPUT);
          digitalWrite(ledPins[i], LOW);
      }
      Serial.println("LED pins configured");

      // === Создание объектов FreeRTOS ===
      xButtonQueue = xQueueCreate(5, sizeof(ButtonEvent_t));
      xSerialMutex = xSemaphoreCreateMutex();

      Serial.printf("Queue: %p, Mutex: %p\n", (void*)xButtonQueue, (void*)xSerialMutex);

      if (xButtonQueue != NULL && xSerialMutex != NULL) {
          Serial.println("Creating FreeRTOS tasks...");
          xTaskCreatePinnedToCore(TaskButtons,       "ButtonsTask",   2048, NULL, 3, NULL, 1);
          xTaskCreatePinnedToCore(TaskGameLogic,     "GameLogicTask", 3072, NULL, 2, NULL, 1);
          xTaskCreatePinnedToCore(TaskSystemMonitor, "MonitorTask",   2048, NULL, 1, NULL, 1);
          xTaskCreatePinnedToCore(TaskDisplay,       "DisplayTask",   4096, NULL, 1, NULL, 1);
          Serial.println("All tasks created!");
      } else {
          Serial.println("ERROR: Queue or Mutex creation failed!");
      }

      Serial.println("=== SETUP COMPLETED ===\n");
  }

  void loop() {
      vTaskDelay(pdMS_TO_TICKS(1000));
  }
