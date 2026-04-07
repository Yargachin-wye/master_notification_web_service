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

// ==================== СЕРДЕЧКИ (TikTok-style) ====================
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


struct FloatingParticle {
  bool active = false;
  int16_t x, y;           // текущие
  int16_t prevX, prevY;   // предыдущие — для стирания
  int8_t vx, vy;
  uint16_t color;
  uint8_t life = 255;     // жизнь частицы (0-255), для плавного затухания
};

#define MAX_PARTICLES 8          // достаточно для красивого эффекта
FloatingParticle Particles[MAX_PARTICLES];

unsigned long lastParticleUpdate = 0;
unsigned long lastSpawnCheck = 0;

// ==================== NTP и погода ====================
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600 * 7;  // GMT+7 (Bangkok, Jakarta, etc)
const int daylightOffset_sec = 0;

// OpenWeatherMap API (бесплатный ключ нужно получить на openweathermap.org)
const char* weatherApiKey = "5d429dd05fc20ad66d43fcc5a5dbb694";  // Замените на свой API ключ
const char* city = "Novosibirsk";              // Замените на свой город
const char* countryCode = "RU";            // Код страны

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
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + 
               String(city) + "," + String(countryCode) + 
               "&appid=" + String(weatherApiKey) + 
               "&units=metric&lang=ru";
  
  Serial.println("🌤️  Запрос погоды...");
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    
    // Простой парсинг JSON
    int tempIndex = payload.indexOf("\"temp\":") + 7;
    int tempEnd = payload.indexOf(",", tempIndex);
    currentWeather.temp = payload.substring(tempIndex, tempEnd).toFloat();
    
    int humIndex = payload.indexOf("\"humidity\":") + 11;
    int humEnd = payload.indexOf(",", humIndex);
    if (humEnd == -1) humEnd = payload.indexOf("}", humIndex);
    currentWeather.humidity = payload.substring(humIndex, humEnd).toInt();
    
    int descIndex = payload.indexOf("\"description\":\"") + 16;
    int descEnd = payload.indexOf("\"", descIndex);
    currentWeather.description = payload.substring(descIndex, descEnd);
    
    lastWeatherUpdate = millis();
    Serial.println("✅ Погода обновлена!");
  } else {
    Serial.printf("❌ Ошибка HTTP: %d\n", httpCode);
    Serial.println("   Проверьте API ключ и подключение");
  }
  
  http.end();
}

void printWeather() {
  if (millis() - lastWeatherUpdate > weatherUpdateInterval) {
    fetchWeather();
  }
  
  // Если погода не была получена ни разу
  if (lastWeatherUpdate == 0) {
    fetchWeather();
  }
  
  Serial.println("\n========== 🌤️  ПОГОДА ==========");
  
  Serial.print(city);
  Serial.print(" ");
  Serial.print(currentWeather.temp);
  Serial.print("°C Humidity: ");
  Serial.print(currentWeather.humidity);
  Serial.print("% ");
  Serial.println(currentWeather.description);
}

// ==================== ОБРАБОТКА КОМАНД ====================
void handleSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();

    if (command == "all") {
      printCurrentTime();
      printWeather();
    }
  }
}
void spawnParticle() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!Particles[i].active) {
      Particles[i].active = true;
      Particles[i].x = random(10, tft.width() - 18);
      Particles[i].y = tft.height() + 5;           // начинаем чуть ниже экрана
      Particles[i].vx = random(-2, 3);             // лёгкое покачивание
      Particles[i].vy = -(3 + random(0, 3));       // скорость вверх
      Particles[i].color = tft.color565(random(200, 255), random(20, 100), random(100, 255));
      Particles[i].life = 255;                     // полная жизнь при спавне
      return;
    }
  }
}

void updateParticles() {
  unsigned long now = millis();
  if (now - lastParticleUpdate < 35) return;   // 35 = ~28 fps
  lastParticleUpdate = now;

  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (Particles[i].active) {
      // Сохраняем текущие координаты как предыдущие ПЕРЕД движением
      Particles[i].prevX = Particles[i].x;
      Particles[i].prevY = Particles[i].y;

      // Двигаем
      Particles[i].x += Particles[i].vx;
      Particles[i].y += Particles[i].vy;

      // Случайное покачивание
      if (random(0, 10) == 0) {
        Particles[i].vx = -Particles[i].vx;
      }

      // Удаляем, если вылетело за экран
      if (Particles[i].y < -15 || Particles[i].x < -10 || Particles[i].x > tft.width() + 10) {
        Particles[i].active = false;
      }
      
      // Плавное затухание при приближении к верху экрана
      if (Particles[i].y < 30) {
        Particles[i].life = (Particles[i].y * 255) / 30; // линейное затухание
        if (Particles[i].life < 10) {
          Particles[i].active = false;
        }
      }
    }
  }
}

void drawParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (Particles[i].active) {
      // 1. Стираем старое положение (рисуем чёрный прямоугольник)
      tft.fillRect(Particles[i].prevX, Particles[i].prevY, pBitmapSize, pBitmapSize, ST77XX_BLACK);

      // 2. Вычисляем цвет с учётом прозрачности (life)
      uint8_t r = (Particles[i].color >> 11) & 0x1F;
      uint8_t g = (Particles[i].color >> 5) & 0x3F;
      uint8_t b = Particles[i].color & 0x1F;
      
      // Применяем life как коэффициент прозрачности (0-255)
      r = (r * Particles[i].life) >> 8;
      g = (g * Particles[i].life) >> 8;
      b = (b * Particles[i].life) >> 8;
      
      uint16_t fadedColor = (r << 11) | (g << 5) | b;

      // 3. Рисуем сердечко на новом месте с учётом затухания
      tft.drawBitmap(Particles[i].x, Particles[i].y, particleBitmap, pBitmapSize, pBitmapSize, fadedColor);
    } 
    else if (Particles[i].prevX != -9999) {   // опционально: дорисовываем стирание при деактивации
      tft.fillRect(Particles[i].prevX, Particles[i].prevY, pBitmapSize, pBitmapSize, ST77XX_BLACK);
      Particles[i].prevX = -9999; // метка, что уже стёрто
    }
  }
}

// ==================== ВЫВОД СООБЩЕНИЯ ====================
void showMessageOnScreen(const String &msg) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.println("Message:");
  tft.println("-------------");

  tft.setTextColor(ST77XX_WHITE);
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
      if (message.indexOf("<3") != -1 || message.indexOf("❤️") != -1) {
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

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(resetButton, INPUT_PULLUP);

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
void loop() {
  webSocket.loop();

  // Обработка Serial команд
  handleSerialCommands();

  // Обновление и отрисовка сердечек
  updateParticles();
  drawParticles();

  // Кнопка сброса
  if (digitalRead(resetButton) == LOW) {
    delay(50);
    if (digitalRead(resetButton) == LOW) resetWiFi();
  }

  // Небольшая пауза, чтобы не нагружать CPU на 100%
  delay(5);   // можно уменьшить до 1-2, если нужно ещё плавнее
}