#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define NUM_ELEMENTS 5

// Пины светодиодов (Красный, Синий, Зеленый, Желтый, Белый)
const uint8_t ledPins[NUM_ELEMENTS]    = {1, 2, 4, 5, 6};
// Пины кнопок, соответствующие светодиодам по индексу
const uint8_t buttonPins[NUM_ELEMENTS] = {7, 15, 16, 17, 18};

#define BUZZER_PIN 8

// Структуры и типы данных
typedef struct {
    int buttonIndex; // Индекс нажатой кнопки (0..4)
} ButtonEvent_t;

// Объекты FreeRTOS
QueueHandle_t xButtonQueue = NULL;
SemaphoreHandle_t xSerialMutex = NULL;

// Глобальное состояние игры
int score = 0;
int currentTimeoutMs = 1500; // Стартовое время на реакцию (1.5 сек)

// --- Вспомогательная функция безопасного логгирования (Mutex) ---
void safeLog(const char* msg) {
    if (xSemaphoreTake(xSerialMutex, portMAX_DELAY) == pdTRUE) {
        Serial.println(msg);
        xSemaphoreGive(xSerialMutex);
    }
}

// --- Задача 1: Опрос кнопок (High Priority) ---
void TaskButtons(void *pvParameters) {
    bool lastState[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        pinMode(buttonPins[i], INPUT_PULLUP);
        lastState[i] = HIGH;
    }

    for (;;) {
        for (int i = 0; i < NUM_ELEMENTS; i++) {
            bool currentState = digitalRead(buttonPins[i]);
            
            // Фиксация нажатия (переход из HIGH в LOW) с простой защитой от дребезга
            if (lastState[i] == HIGH && currentState == LOW) {
                vTaskDelay(pdMS_TO_TICKS(20)); // Антидребезг 20 мс
                if (digitalRead(buttonPins[i]) == LOW) {
                    ButtonEvent_t event;
                    event.buttonIndex = i;
                    // Отправка события в очередь
                    xQueueSend(xButtonQueue, &event, 0);
                }
            }
            lastState[i] = currentState;
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Частота опроса 100 Гц
    }
}

// --- Задача 2: Основная логика игры (Medium Priority) ---
void TaskGameLogic(void *pvParameters) {
    ButtonEvent_t rxEvent;
    
    // Начальная пауза перед стартом
    vTaskDelay(pdMS_TO_TICKS(2000));
    safeLog("=== GAME STARTED ===");

    for (;;) {
        // 1. Выбираем случайный LED (0..4)
        int targetIndex = random(0, NUM_ELEMENTS);
        
        // Включаем целевой LED
        digitalWrite(ledPins[targetIndex], HIGH);
        
        char logBuf[64];
        snprintf(logBuf, sizeof(logBuf), "\n[ROUND] Light LED #%d (%ms limit)", targetIndex + 1, currentTimeoutMs);
        safeLog(logBuf);

        // Очищаем старые нажатия из очереди перед новым раундом
        xQueueReset(xButtonQueue);

        // 2. Ждем нажатия из очереди С ТАЙМАУТОМ
        // Функция сразу отдает управление, если кнопка нажата, либо ждет ровно currentTimeoutMs
        BaseType_t xStatus = xQueueReceive(xButtonQueue, &rxEvent, pdMS_TO_TICKS(currentTimeoutMs));

        // Гасим LED сразу после ответа или таймаута
        digitalWrite(ledPins[targetIndex], LOW);

        // 3. Проверка результата
        if (xStatus == pdTRUE) {
            if (rxEvent.buttonIndex == targetIndex) {
                // УСПЕХ: Нажата верная кнопка вовремя
                score++;
                
                // Звуковой сигнал успеха (1000 Гц, 100 мс)
                tone(BUZZER_PIN, 1000, 100);

                snprintf(logBuf, sizeof(logBuf), "-> SUCCESS! Score: %d", score);
                safeLog(logBuf);

                // Усложнение: немного уменьшаем время на реакцию (минимум 400 мс)
                if (currentTimeoutMs > 400) {
                    currentTimeoutMs -= 25;
                }
            } else {
                // ОШИБКА: Нажата не та кнопка
                tone(BUZZER_PIN, 200, 300); // Низкий гудок
                
                snprintf(logBuf, sizeof(logBuf), "-> WRONG BUTTON! Pressed #%d. Score: %d", rxEvent.buttonIndex + 1, score);
                safeLog(logBuf);
            }
        } else {
            // ТАЙМАУТ: Игрок не успел нажать
            tone(BUZZER_PIN, 200, 300); // Низкий гудок

            snprintf(logBuf, sizeof(logBuf), "-> TIMEOUT! You were too slow. Score: %d", score);
            safeLog(logBuf);
        }

        // Пауза между раундами (1 секунда)
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// --- Задача 3: Системный монитор / Heartbeat (Low Priority) ---
void TaskSystemMonitor(void *pvParameters) {
    for (;;) {
        // Фоновая задача каждые 10 секунд выводит статус памяти
        char logBuf[64];
        snprintf(logBuf, sizeof(logBuf), "[SYSTEM] Free Heap: %d bytes", ESP.getFreeHeap());
        safeLog(logBuf);

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void setup() {
    Serial.begin(115200);
    randomSeed(analogRead(0)); // Инициализация генератора случайных чисел

    // Инициализация пинов светодиодов и пищалки
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        pinMode(ledPins[i], OUTPUT);
        digitalWrite(ledPins[i], LOW);
    }
    pinMode(BUZZER_PIN, OUTPUT);

    // Создаем FreeRTOS объекты
    xButtonQueue = xQueueCreate(5, sizeof(ButtonEvent_t));
    xSerialMutex  = xSemaphoreCreateMutex();

    if (xButtonQueue != NULL && xSerialMutex != NULL) {
        // Создание задач с разным приоритетом
        xTaskCreatePinnedToCore(TaskButtons,       "ButtonsTask",   2048, NULL, 3, NULL, 1);
        xTaskCreatePinnedToCore(TaskGameLogic,     "GameLogicTask", 3072, NULL, 2, NULL, 1);
        xTaskCreatePinnedToCore(TaskSystemMonitor, "MonitorTask",   2048, NULL, 1, NULL, 1);
    }
}

void loop() {
    // В FreeRTOS пустой loop() отдаёт оставшийся квант времени IDLE-задаче
    vTaskDelay(pdMS_TO_TICKS(1000));
}
