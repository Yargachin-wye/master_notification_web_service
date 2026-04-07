#include <WiFi.h>
#include <WiFiManager.h>
#include <WebSocketsClient.h>
#include <Adafruit_ST7735.h>

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
    }
  }
}

void drawParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (Particles[i].active) {
      // 1. Стираем старое положение (рисуем чёрный прямоугольник)
      tft.fillRect(Particles[i].prevX, Particles[i].prevY, pBitmapSize, pBitmapSize, ST77XX_BLACK);

      // 2. Рисуем сердечко на новом месте
      tft.drawBitmap(Particles[i].x, Particles[i].y, particleBitmap, pBitmapSize, pBitmapSize, Particles[i].color);
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

  // Небольшая пауза, чтобы не нагружать CPU на 100%
  delay(5);   // можно уменьшить до 1-2, если нужно ещё плавнее
}