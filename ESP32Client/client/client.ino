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

const int artSize = 16;

// ==================== СЕРДЕЧКИ (TikTok-style) ====================
const uint8_t artBitmap[] PROGMEM = {
  0b00011100, 0b00111000,
  0b00111110, 0b01111100,
  0b01111111, 0b11111110,
  0b11111111, 0b11111111,
  0b11111111, 0b11111111,
  0b11111111, 0b11111111,
  0b11111111, 0b11111111,
  0b01111111, 0b11111110,
  0b00111111, 0b11111100,
  0b00011111, 0b11111000,
  0b00001111, 0b11110000,
  0b00000111, 0b11100000,
  0b00000011, 0b11000000,
  0b00000001, 0b10000000,
  0b00000000, 0b00000000,
  0b00000000, 0b00000000
};


struct FloatingHeart {
  bool active = false;
  int16_t x, y;           // текущие
  int16_t prevX, prevY;   // предыдущие — для стирания
  int8_t vx, vy;
  uint16_t color;
};

#define MAX_HEARTS 8          // достаточно для красивого эффекта
FloatingHeart hearts[MAX_HEARTS];

unsigned long lastHeartUpdate = 0;
unsigned long lastSpawnCheck = 0;

// ==================== ФУНКЦИИ СЕРДЕЧЕК ====================
void spawnHeart() {
  for (int i = 0; i < MAX_HEARTS; i++) {
    if (!hearts[i].active) {
      hearts[i].active = true;
      hearts[i].x = random(10, tft.width() - 18);
      hearts[i].y = tft.height() + 5;           // начинаем чуть ниже экрана
      hearts[i].vx = random(-2, 3);             // лёгкое покачивание
      hearts[i].vy = -(3 + random(0, 3));       // скорость вверх
      hearts[i].color = tft.color565(random(200, 255), random(20, 100), random(100, 255));
      return;
    }
  }
}

void updateHearts() {
  unsigned long now = millis();
  if (now - lastHeartUpdate < 35) return;   // 35 = ~28 fps
  lastHeartUpdate = now;

  for (int i = 0; i < MAX_HEARTS; i++) {
    if (hearts[i].active) {
      // Сохраняем текущие координаты как предыдущие ПЕРЕД движением
      hearts[i].prevX = hearts[i].x;
      hearts[i].prevY = hearts[i].y;

      // Двигаем
      hearts[i].x += hearts[i].vx;
      hearts[i].y += hearts[i].vy;

      // Случайное покачивание
      if (random(0, 10) == 0) {
        hearts[i].vx = -hearts[i].vx;
      }

      // Удаляем, если вылетело за экран
      if (hearts[i].y < -15 || hearts[i].x < -10 || hearts[i].x > tft.width() + 10) {
        hearts[i].active = false;
      }
    }
  }
}

void drawHearts() {
  for (int i = 0; i < MAX_HEARTS; i++) {
    if (hearts[i].active) {
      // 1. Стираем старое положение (рисуем чёрный прямоугольник)
      tft.fillRect(hearts[i].prevX, hearts[i].prevY, artSize, artSize, ST77XX_BLACK);

      // 2. Рисуем сердечко на новом месте
      tft.drawBitmap(hearts[i].x, hearts[i].y, artBitmap, artSize, artSize, hearts[i].color);
    } 
    else if (hearts[i].prevX != -9999) {   // опционально: дорисовываем стирание при деактивации
      tft.fillRect(hearts[i].prevX, hearts[i].prevY, artSize, artSize, ST77XX_BLACK);
      hearts[i].prevX = -9999; // метка, что уже стёрто
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
          spawnHeart();
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
  for (int i = 0; i < MAX_HEARTS; i++) hearts[i].active = false;

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
  updateHearts();
  drawHearts();

  // Кнопка сброса
  if (digitalRead(resetButton) == LOW) {
    delay(50);
    if (digitalRead(resetButton) == LOW) resetWiFi();
  }

  // Небольшая пауза, чтобы не нагружать CPU на 100%
  delay(5);   // можно уменьшить до 1-2, если нужно ещё плавнее
}