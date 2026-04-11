#include <WiFi.h>
#include <WiFiManager.h>
#include <WebSocketsClient.h>
#include <Adafruit_ST7735.h>
#include <HTTPClient.h>
#include <time.h>

// ==================== TFT ПИНЫ ====================
#define TFT_CS    5
#define TFT_RST   22
#define TFT_DC    21
#define TFT_SCLK  18
#define TFT_MOSI  23
// Энкодер (A, B, кнопка)
#define ENC_A     25
#define ENC_B     26
#define ENC_BTN   27

// ==================== RGB СВЕТОДИОД ====================
#define LED_R 12
#define LED_G 13
#define LED_B 14

const bool LED_COMMON_ANODE = false;

static inline uint8_t ledLevel(uint8_t v) {
  return v;
}

static inline void setLED(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(LED_R, ledLevel(r));
  analogWrite(LED_G, ledLevel(g));
  analogWrite(LED_B, ledLevel(b));
}

// ==================== ВСПЫШКА СВЕТОДИОДА ====================
unsigned long ledOffAtMs = 0;

static inline void updateLEDFlash() {
  if (ledOffAtMs != 0 && (long)(millis() - ledOffAtMs) >= 0) {
    setLED(0, 0, 0);
    ledOffAtMs = 0;
  }
}

void flashLED(uint8_t r, uint8_t g, uint8_t b, int duration = 60) {
  setLED(r, g, b);
  ledOffAtMs = millis() + (unsigned long)duration;
}

// ==================== WiFi и WebSocket ====================
WiFiManager wm;
WiFiManagerParameter custom_server_host(
  "server_host",
  "Домен сервера на render.com",
  "master-notification-web-service.onrender.com",
  60
);
WebSocketsClient webSocket;

// Экран
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

const int pBitmapSize = 8;

// ==================== NTP и погода ====================
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600 * 7;  // GMT+7 (Bangkok, Jakarta, etc)
const int daylightOffset_sec = 0;

struct WeatherData {
  float temp;
  int humidity;
  String description;
  String icon;
};

WeatherData currentWeather;
unsigned long lastWeatherUpdate = 0;
const unsigned long weatherUpdateInterval = 600000; // 10 минут

// ==================== ФУНКЦИИ ВРЕМЕНИ ====================
void initTime() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("⏰ Синхронизация времени...");
  
  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 10) {
    Serial.print(".");
    delay(500);
    attempts++;
  }
  
  if (attempts < 10) {
    Serial.println("\n✅ Время синхронизировано!");
  } else {
    Serial.println("\n❌ Не удалось синхронизировать время");
  }
}

void printCurrentTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("❌ Не удалось получить время");
    return;
  }
  
  char timeString[25];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
  
  Serial.println("\n========== 📅 ТЕКУЩЕЕ ВРЕМЯ ==========");
  Serial.print("Дата: ");
  Serial.println(timeString);
  Serial.print("День недели: ");
  switch(timeinfo.tm_wday) {
    case 0: Serial.println("Воскресенье"); break;
    case 1: Serial.println("Понедельник"); break;
    case 2: Serial.println("Вторник"); break;
    case 3: Serial.println("Среда"); break;
    case 4: Serial.println("Четверг"); break;
    case 5: Serial.println("Пятница"); break;
    case 6: Serial.println("Суббота"); break;
  }
  Serial.println("=====================================\n");
}

// ==================== ФУНКЦИИ ПОГОДЫ ====================
void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Нет подключения к WiFi");
    return;
  }

  HTTPClient http;
  String host = custom_server_host.getValue();
  // Убираем https:// или http:// если есть
  if (host.startsWith("https://")) host = host.substring(8);
  if (host.startsWith("http://")) host = host.substring(7);

  String url = "https://" + host + "/weather";

  Serial.println("🌤️  Запрос погоды...");
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();

    // Парсинг JSON
    int tempIndex = payload.indexOf("\"temp\":") + 7;
    int tempEnd = payload.indexOf(",", tempIndex);
    if (tempEnd == -1) tempEnd = payload.indexOf("}", tempIndex);
    currentWeather.temp = payload.substring(tempIndex, tempEnd).toFloat();

    int humIndex = payload.indexOf("\"humidity\":") + 11;
    int humEnd = payload.indexOf(",", humIndex);
    if (humEnd == -1) humEnd = payload.indexOf("}", humIndex);
    currentWeather.humidity = payload.substring(humIndex, humEnd).toInt();

    int descIndex = payload.indexOf("\"description\":\"") + 15;
    if (descIndex > 15) {
      int descEnd = payload.indexOf("\"", descIndex);
      currentWeather.description = payload.substring(descIndex, descEnd);
    }

    lastWeatherUpdate = millis();
    Serial.println("✅ Погода обновлена!");
  } else {
    Serial.printf("❌ Ошибка HTTP: %d\n", httpCode);
  }

  http.end();
}

// ==================== ЧАСТИЦЫ ====================

const uint8_t particleBitmap[] PROGMEM = {
  0b01100110,
  0b11111111,
  0b11111111,
  0b11111111,
  0b01111110,
  0b00111100,
  0b00011000,
  0b00000000
};

#define PARTICLE_SIZE            8      // размер битмапа (pBitmapSize)
const int16_t FADE_START_Y     = 90; // с какого y начинаем плавное затухание
const int16_t FULL_DISAPPEAR_Y = 60;  // полностью исчезают здесь

struct FloatingParticle {
  bool active = false;
  int16_t x = 0, y = 0;
  int16_t prevX = -9999, prevY = -9999;  // -9999 = "нет предыдущей позиции"
  int8_t vx = 0, vy = 0;
  uint16_t color = 0;
  uint8_t life = 255;
};

#define MAX_PARTICLES 8
FloatingParticle Particles[MAX_PARTICLES];

unsigned long lastParticleUpdate = 0;

void spawnParticle() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!Particles[i].active) {
      Particles[i].active = true;
      Particles[i].x = random(10, tft.width() - PARTICLE_SIZE - 2);
      Particles[i].y = tft.height() + 5;

      // Сразу сохраняем текущие координаты как prev (чтобы первый draw не стирал мусор)
      Particles[i].prevX = Particles[i].x;
      Particles[i].prevY = Particles[i].y;

      Particles[i].vx = random(-2, 3);
      Particles[i].vy = -(3 + random(0, 3));

      // Генерация цвета
      uint8_t r = random(200, 255);
      uint8_t g = random(20, 100);
      uint8_t b = random(100, 255);
      Particles[i].color = tft.color565(r, g, b);
      Particles[i].life = 255;

      flashLED(r, g/2, b);
      return;
    }
  }
}

void updateParticles() {
  unsigned long now = millis();
  if (now - lastParticleUpdate < 35) return;   // ~28 fps
  lastParticleUpdate = now;

  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (Particles[i].active) {
      // Сохраняем старые координаты ПЕРЕД движением
      Particles[i].prevX = Particles[i].x;
      Particles[i].prevY = Particles[i].y;

      // Движение
      Particles[i].x += Particles[i].vx;
      Particles[i].y += Particles[i].vy;

      // Случайное покачивание
      if (random(0, 10) == 0) {
        Particles[i].vx = -Particles[i].vx;
      }

      // Удаление за пределами экрана
      if (Particles[i].y < -15 || 
          Particles[i].x < -10 || 
          Particles[i].x > tft.width() + 10) {
        Particles[i].active = false;
        continue;
      }

      // === НОВАЯ ЛОГИКА ЗАТУХАНИЯ ===
      if (Particles[i].y <= FULL_DISAPPEAR_Y) {
        Particles[i].life = 0;
        Particles[i].active = false;
      } 
      else if (Particles[i].y < FADE_START_Y) {
        // Плавное затухание от 120 до 60 пикселей
        Particles[i].life = ((uint16_t)(Particles[i].y - FULL_DISAPPEAR_Y) * 255UL) / (FADE_START_Y - FULL_DISAPPEAR_Y);
      }
      // если y >= 120 — life остаётся 255 (полная яркость)
    }
  }
}

void drawParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (Particles[i].active) {
      // 1. Стираем предыдущее положение
      tft.fillRect(Particles[i].prevX, Particles[i].prevY, 
                   PARTICLE_SIZE, PARTICLE_SIZE, ST77XX_BLACK);

      // 2. Применяем затухание (life)
      uint8_t r = (Particles[i].color >> 11) & 0x1F;
      uint8_t g = (Particles[i].color >> 5) & 0x3F;
      uint8_t b = Particles[i].color & 0x1F;

      r = (r * Particles[i].life) >> 8;
      g = (g * Particles[i].life) >> 8;
      b = (b * Particles[i].life) >> 8;

      uint16_t fadedColor = (r << 11) | (g << 5) | b;

      // 3. Рисуем сердечко
      tft.drawBitmap(Particles[i].x, Particles[i].y, 
                     particleBitmap, PARTICLE_SIZE, PARTICLE_SIZE, fadedColor);
    } 
    else if (Particles[i].prevX != -9999) {
      // Стираем последнее положение при деактивации
      tft.fillRect(Particles[i].prevX, Particles[i].prevY, 
                   PARTICLE_SIZE, PARTICLE_SIZE, ST77XX_BLACK);
      Particles[i].prevX = -9999;
      Particles[i].prevY = -9999;
    }
  }
}

// ==================== ВЫВОД ДАТЫ/ВРЕМЕНИ И ПОГОДЫ НА ЭКРАН ====================
void displayDateTimeWeather() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return;
  }
  
  // Обновляем погоду если нужно
  if (millis() - lastWeatherUpdate > weatherUpdateInterval || lastWeatherUpdate == 0) {
    fetchWeather();
  }
  
  char timeStr[20];
  strftime(timeStr, sizeof(timeStr), "%d.%m %H:%M", &timeinfo);
  
  // Стираем область для даты/времени и погоды (2 строки)
  tft.fillRect(0, 0, tft.width(), 32, ST77XX_BLACK);
  
  // Строка 1: Дата и время
  tft.setCursor(0, 0);
  tft.setTextColor(0xfedd);
  tft.setTextSize(2);
  tft.print(timeStr);
  
  // Строка 2: Погода
  tft.setCursor(0, 16);
  tft.setTextColor(0xa45f);
  tft.setTextSize(2);
  tft.print(currentWeather.temp, 0);
  tft.print((char)247); // символ градуса
  tft.print("C ");
  tft.print(currentWeather.humidity);
  tft.print("% ");
  tft.setCursor(48, 32);
  tft.setTextSize(1);
  tft.print(currentWeather.description);
}

void showMessageOnScreen(const String &msg) {
  // Оставляем верхние 2 строки (32 пикселя) для даты/времени и погоды
  tft.fillRect(0, 32, tft.width(), tft.height() - 32, ST77XX_BLACK);
  tft.setCursor(0, 34);
  tft.setTextColor(0xb01f);
  tft.setTextSize(2);
  tft.println("Msg:");
  flashLED(0, 255, 0);

  tft.setTextColor(0xd6bf);
  int start = 0;
  while (start < msg.length()) {
    int end = start + 26;
    if (end > msg.length()) end = msg.length();
    tft.println(msg.substring(start, end));
    start = end;
  }
}

// ==================== WebSocket ====================
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("❌ WebSocket отключён");
      tft.fillScreen(ST77XX_BLACK);
      tft.setCursor(0, 0);
      tft.setTextColor(ST77XX_RED);
      tft.setTextSize(2);
      tft.println("WebSocket off");
      break;

    case WStype_CONNECTED:
      Serial.println("✅ WebSocket подключён!");
      tft.fillScreen(ST77XX_BLACK);
      tft.setCursor(0, 0);
      tft.setTextColor(ST77XX_GREEN);
      tft.setTextSize(2);
      tft.println("WebSocket connected");
      break;

    case WStype_TEXT: {
      String message = String((char*)payload).substring(0, length);
      Serial.print("📨 Получено: ");
      Serial.println(message);
      

      // === ЗАПУСК СЕРДЕЧЕК, если в сообщении есть <3 ===
      if (message.indexOf("<3") != -1) {
        // запускаем сразу 3–5 сердечек для красивого эффекта
        for (int i = 0; i < 1; i++) {
          spawnParticle();
        }
      }else{
        showMessageOnScreen(message);
      }
      break;
    }
  }
}

// ==================== СБРОС ====================
const int resetButton = 0;

void resetWiFi() {
  wm.resetSettings();
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(resetButton, INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);

  // Инициализация RGB-светодиода
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  setLED(0, 0, 0);

  Serial.println("\n=== ESP32 + WebSocket + Сердечки ===");

  // Инициализация экрана
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(0, 0);
  tft.println("Starting...");

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  wm.addParameter(&custom_server_host);
  wm.setConfigPortalTimeout(300);
  wm.setConnectTimeout(30);

  bool res = wm.autoConnect("ESP32-Setup", "12345678");
  if (!res) ESP.restart();

  Serial.println("✅ WiFi подключён");
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_CYAN);
  tft.println("WiFi connected");
  tft.println(WiFi.localIP());

  // Инициализация сердечек
  for (int i = 0; i < MAX_PARTICLES; i++) Particles[i].active = false;

  // WebSocket
  String host = custom_server_host.getValue();
  Serial.print("Подключаемся к wss://");
  Serial.println(host);
  webSocket.beginSSL(host.c_str(), 443, "/");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);

  // Инициализация времени
  initTime();
  
  // Приветственное сообщение
  Serial.println("\n📱 'Умные часы' готовы!");
  Serial.println("Введите 'help' для списка команд\n");
}

// ==================== LOOP (НЕБЛОКИРУЮЩИЙ) ====================
unsigned long lastTimeWeatherUpdate = 0;
const unsigned long timeWeatherInterval = 60000; // обновление каждую минуту

void loop() {
  webSocket.loop();

  updateLEDFlash();

  // Обновление и отрисовка сердечек
  updateParticles();
  drawParticles();

  // Обновление даты/времени и погоды
  unsigned long now = millis();
  if (now - lastTimeWeatherUpdate > timeWeatherInterval) {
    displayDateTimeWeather();
    lastTimeWeatherUpdate = now;
  }

  // Кнопка сброса
  if (digitalRead(resetButton) == LOW) {
    delay(50);
    if (digitalRead(resetButton) == LOW) resetWiFi();
  }

  // Кнопка энкодера - обновление времени и погоды
  if (digitalRead(ENC_BTN) == LOW) {
    delay(50); // антидребезг
    if (digitalRead(ENC_BTN) == LOW) {
      Serial.println(" Обновление времени и погоды...");
      displayDateTimeWeather();
      lastTimeWeatherUpdate = millis();
      // Ждем отпускания кнопки
      while (digitalRead(ENC_BTN) == LOW) delay(10);
    }
  }

  // Небольшая пауза, чтобы не нагружать CPU на 100%
  delay(5);   // можно уменьшить до 1-2, если нужно ещё плавнее
}