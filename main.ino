#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define NUM_ELEMENTS 5

// Пины светодиодов (Красный, Синий, Зеленый, Желтый, Белый)
const uint8_t ledPins[NUM_ELEMENTS]    = { 4, 5, 6, 7, 15};
// Пины кнопок, соответствующие светодиодам по индексу
const uint8_t buttonPins[NUM_ELEMENTS] = {16, 17, 18, 8, 9};

// Структура для передачи событий кнопок
typedef struct {
    int buttonIndex; // Индекс нажатой кнопки (0..4)
} ButtonEvent_t;

// Объекты FreeRTOS
QueueHandle_t xButtonQueue = NULL;
SemaphoreHandle_t xSerialMutex = NULL;

// Глобальные переменные состояния игры
int score = 0;
int currentTimeoutMs = 1500; // Начальный лимит времени на реакцию (1.5 сек)

// --- Вспомогательная функция безопасного логгирования (Mutex) ---
void safeLog(const char* msg) {
    if (xSemaphoreTake(xSerialMutex, portMAX_DELAY) == pdTRUE) {
        Serial.println(msg);
        xSemaphoreGive(xSerialMutex);
    }
}

// --- Задача 1: Опрос кнопок (High Priority = 3) ---
void TaskButtons(void *pvParameters) {
    bool lastState[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        pinMode(buttonPins[i], INPUT_PULLUP);
        lastState[i] = HIGH;
    }

    for (;;) {
        for (int i = 0; i < NUM_ELEMENTS; i++) {
            bool currentState = digitalRead(buttonPins[i]);
            
            // Фиксация нажатия (переход из HIGH в LOW) с защитой от дребезга
            if (lastState[i] == HIGH && currentState == LOW) {
                vTaskDelay(pdMS_TO_TICKS(20)); // Антидребезг 20 мс
                if (digitalRead(buttonPins[i]) == LOW) {
                    ButtonEvent_t event;
                    event.buttonIndex = i;
                    // Отправка индекса нажатой кнопки в очередь
                    xQueueSend(xButtonQueue, &event, 0);
                }
            }
            lastState[i] = currentState;
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Частота опроса 100 Гц
    }
}

// --- Задача 2: Основная логика игры (Medium Priority = 2) ---
void TaskGameLogic(void *pvParameters) {
    ButtonEvent_t rxEvent;
    
    // Пауза перед стартом
    vTaskDelay(pdMS_TO_TICKS(2000));
    safeLog("=== GAME STARTED ===");

    for (;;) {
        // 1. Выбираем случайный LED (0..4)
        int targetIndex = random(0, NUM_ELEMENTS);
        
        // Включаем целевой LED
        digitalWrite(ledPins[targetIndex], HIGH);
        
        char logBuf[80];
        snprintf(logBuf, sizeof(logBuf), "\n[ROUND] Light LED #%d (%d ms limit)", targetIndex + 1, currentTimeoutMs);
        safeLog(logBuf);

        // Сбрасываем накопленные старые нажатия из очереди перед новым раундом
        xQueueReset(xButtonQueue);

        // 2. Ждем ответа из очереди с ТАЙМАУТОМ
        BaseType_t xStatus = xQueueReceive(xButtonQueue, &rxEvent, pdMS_TO_TICKS(currentTimeoutMs));

        // Гасим LED сразу после ответа или таймаута
        digitalWrite(ledPins[targetIndex], LOW);

        // 3. Обработка результата
        if (xStatus == pdTRUE) {
            if (rxEvent.buttonIndex == targetIndex) {
                // УСПЕХ: Нажата правильная кнопка
                score++;
                snprintf(logBuf, sizeof(logBuf), "-> SUCCESS! Score: %d", score);
                safeLog(logBuf);

                // Усложнение: немного уменьшаем время на реакцию (минимум 400 мс)
                if (currentTimeoutMs > 400) {
                    currentTimeoutMs -= 25;
                }
            } else {
                // ОШИБКА: Нажата не та кнопка
                snprintf(logBuf, sizeof(logBuf), "-> WRONG BUTTON! Pressed #%d. Score: %d", rxEvent.buttonIndex + 1, score);
                safeLog(logBuf);
            }
        } else {
            // ТАЙМАУТ: Игрок не успел нажать
            snprintf(logBuf, sizeof(logBuf), "-> TIMEOUT! You were too slow. Score: %d", score);
            safeLog(logBuf);
        }

        // Пауза между раундами (1 секунда)
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// --- Задача 3: Системный монитор / Heartbeat (Low Priority = 1) ---
void TaskSystemMonitor(void *pvParameters) {
    for (;;) {
        // Фоновый отчёт каждые 10 секунд
        char logBuf[64];
        snprintf(logBuf, sizeof(logBuf), "[SYSTEM] Free Heap: %d bytes", ESP.getFreeHeap());
        safeLog(logBuf);

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void setup() {
    Serial.begin(115200);
    randomSeed(analogRead(0)); // Инициализация ГПСЧ

    // Инициализация пинов светодиодов
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        pinMode(ledPins[i], OUTPUT);
        digitalWrite(ledPins[i], LOW);
    }

    // Создаем объекты FreeRTOS
    xButtonQueue = xQueueCreate(5, sizeof(ButtonEvent_t));
    xSerialMutex  = xSemaphoreCreateMutex();

    if (xButtonQueue != NULL && xSerialMutex != NULL) {
        // Создание задач с приоритетами 3, 2 и 1
        xTaskCreatePinnedToCore(TaskButtons,       "ButtonsTask",   2048, NULL, 3, NULL, 1);
        xTaskCreatePinnedToCore(TaskGameLogic,     "GameLogicTask", 3072, NULL, 2, NULL, 1);
        xTaskCreatePinnedToCore(TaskSystemMonitor, "MonitorTask",   2048, NULL, 1, NULL, 1);
    }
}

void loop() {
    // Освобождаем процессорное время для фоновых процессов FreeRTOS
    vTaskDelay(pdMS_TO_TICKS(1000));
}
