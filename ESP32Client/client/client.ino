#include <WiFi.h>
#include <WiFiManager.h>
#include <WebSocketsClient.h>
#include <Adafruit_ST7735.h>
#include <HTTPClient.h>

// ==================== TFT ПИНЫ ====================
#define TFT_CS    5
#define TFT_RST   22
#define TFT_DC    21
#define TFT_SCLK  18
#define TFT_MOSI  23

// ==================== ЭНКОДЕР ПИНЫ ====================
#define ENC_A     25
#define ENC_B     26
#define ENC_BTN   27

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

// ==================== ЧАСТИЦЫ ====================
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

// ==================== ФУНКЦИИ ЧАСТИЦ ====================
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

// ==================== ПОГОДА ====================
void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi не подключен");
    return;
  }

  HTTPClient http;
  String serverHost = custom_server_host.getValue();
  
  // Используем HTTP для локального сервера или HTTPS для render.com
  String url;
  if (serverHost.indexOf("localhost") >= 0 || serverHost.indexOf("192.168.") >= 0) {
    url = "http://" + serverHost + "/weather";
  } else {
    url = "https://" + serverHost + "/weather";
  }
  
  Serial.println("\n--- Запрос погоды ---");
  Serial.print("URL: ");
  Serial.println(url);
  
  http.begin(url);
  http.setTimeout(5000);
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    Serial.println("Ответ сервера:");
    Serial.println(payload);
    
    // Простой парсинг значений
    int cityStart = payload.indexOf("\"city\":\"") + 9;
    int cityEnd = payload.indexOf("\"", cityStart);
    String city = payload.substring(cityStart, cityEnd);
    
    int timeStart = payload.indexOf("\"time\":\"") + 9;
    int timeEnd = payload.indexOf("\"", timeStart);
    String time = payload.substring(timeStart, timeEnd);
    
    int tempStart = payload.indexOf("\"temperature\":") + 15;
    int tempEnd = payload.indexOf(",", tempStart);
    String temp = payload.substring(tempStart, tempEnd);
    
    Serial.print("Город: ");
    Serial.println(city);
    Serial.print("Время: ");
    Serial.println(time);
    Serial.print("Температура: ");
    Serial.print(temp);
    Serial.println("C");
    
    // Выводим на экран
    String displayText = city + "\n" + time + "\n" + temp + "C";
    showMessageOnScreen(displayText);
  } else {
    Serial.print("Ошибка HTTP: ");
    Serial.println(httpCode);
    showMessageOnScreen("Ошибка погоды\nHTTP: " + String(httpCode));
  }
  
  http.end();
  Serial.println("---------------------\n");
}

// ==================== СБРОС ====================
const int resetButton = 0;

// Дебаунс кнопки энкодера
unsigned long lastBtnPress = 0;
const unsigned long BTN_DEBOUNCE = 200;

void resetWiFi() {
  wm.resetSettings();
  ESP.restart();
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(resetButton, INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);

  Serial.println("\n=== ESP32 + WebSocket + Погода ===");

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
}

// ==================== LOOP (НЕБЛОКИРУЮЩИЙ) ====================
void loop() {
  webSocket.loop();

  // Обновление и отрисовка сердечек
  updateParticles();
  drawParticles();

  // Кнопка сброса
  if (digitalRead(resetButton) == LOW) {
    delay(50);
    if (digitalRead(resetButton) == LOW) resetWiFi();
  }

  // Кнопка энкодера - запрос погоды
  if (digitalRead(ENC_BTN) == LOW) {
    unsigned long now = millis();
    if (now - lastBtnPress > BTN_DEBOUNCE) {
      lastBtnPress = now;
      Serial.println("[ENC] Кнопка нажата - запрашиваем погоду");
      fetchWeather();
    }
  }

  // Небольшая пауза, чтобы не нагружать CPU на 100%
  delay(5);   // можно уменьшить до 1-2, если нужно ещё плавнее
}