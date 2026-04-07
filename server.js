const express = require('express');
const http = require('http');
const WebSocket = require('ws');

const app = express();
const server = http.createServer(app);
const wss = new WebSocket.Server({ server });

const clients = new Set();

// ==================== WebSocket ====================
wss.on('connection', (ws) => {
    console.log('ESP32 или клиент подключился');
    clients.add(ws);

    ws.send('Connected to the server successfully!');

    ws.on('message', (msg) => {
        console.log('Сообщение от клиента:', msg.toString());
    });

    ws.on('close', () => {
        clients.delete(ws);
        console.log('Клиент отключился');
    });
});

// ==================== HTTP-эндпоинты (для кнопки) ====================
app.use(express.json());
app.use(express.urlencoded({ extended: true }));

// Простая страница с кнопкой
app.get('/', (req, res) => {
    res.send(`
    <!DOCTYPE html>
    <html lang="ru">
    <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <title>Уведомления для ESP32</title>
      <style>
        body { font-family: Arial; text-align: center; padding: 50px; background: #1e1e1e; color: #0f0; }
        button { padding: 15px 30px; font-size: 18px; margin: 10px; cursor: pointer; }
        input { padding: 10px; width: 300px; font-size: 16px; margin: 10px; }
        .log { margin-top: 30px; text-align: left; max-width: 600px; margin-left: auto; margin-right: auto; color: #aaa; }
      </style>
    </head>
    <body>
      <h1>Отправка уведомления на ESP32</h1>
      
      <input type="text" id="msg" placeholder="Введите текст уведомления" value="Hello world!">
      <br>
      <button onclick="sendLike()">❤️ Отправить "Лайк"</button>
      <button onclick="sendCustom()">Отправить своё сообщение</button>
      <button onclick="sendHello()">Отправить "Привет"</button>

      <div class="log" id="log"></div>

      <script>
        function sendNotification(text) {
          fetch('/notify', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ message: text })
          })
          .then(r => r.text())
          .then(data => {
            document.getElementById('log').innerHTML += '<br>✓ ' + data;
          });
        }

        function sendLike() { sendNotification('<3'); }
        function sendHello() { sendNotification('Hi!'); }
        function sendCustom() {
          const text = document.getElementById('msg').value || 'Пустое сообщение';
          sendNotification(text);
        }
      </script>
    </body>
  </html>
  `);
});

// Эндпоинт для отправки уведомления
app.post('/notify', (req, res) => {
    const { message } = req.body;

    if (!message) {
        return res.status(400).send('Сообщение пустое');
    }

    let sentCount = 0;
    clients.forEach(client => {
        if (client.readyState === WebSocket.OPEN) {
            client.send(message);
            sentCount++;
        }
    });

    console.log(`Уведомление отправлено ${sentCount} клиентам: ${message}`);
    res.send(`Уведомление "${message}" отправлено ${sentCount} устройствам`);
});

// Эндпоинт для погоды (Новосибирск)
app.get('/weather', (req, res) => {
    const weatherData = {
        temp: 25.5,
        humidity: 60,
        description: "clear"
    };
    res.json(weatherData);
});

const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
    console.log(`Сервер запущен на порту ${PORT}`);
    console.log(`Открой в браузере: http://localhost:${PORT}`);
});