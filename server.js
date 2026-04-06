const WebSocket = require('ws');
const port = process.env.PORT || 3000;   // Render сам подставит PORT

const wss = new WebSocket.Server({
    port: port,
    path: '/'                    // можно изменить на '/ws', если хочешь
});

console.log(`WebSocket сервер запущен на порту ${port}`);

let clients = new Set();

// Когда ESP32 или любой клиент подключается
wss.on('connection', (ws) => {
    console.log('Клиент подключился (ESP32)');
    clients.add(ws);

    ws.send('Подключение к серверу успешно! Готов принимать уведомления.');

    // Если ESP32 что-то отправляет
    ws.on('message', (message) => {
        console.log('Получено от клиента:', message.toString());
    });

    ws.on('close', () => {
        console.log('Клиент отключился');
        clients.delete(ws);
    });
});

// Функция, которую ты будешь вызывать, чтобы отправить уведомление на ESP32
function sendNotification(message) {
    clients.forEach(client => {
        if (client.readyState === WebSocket.OPEN) {
            client.send(message);
        }
    });
}

// Пример: отправляем тестовое сообщение каждые 30 секунд (можно удалить)
setInterval(() => {
    sendNotification('Тестовое уведомление от сервера');
}, 30000);

console.log('Сервер готов. Отправляй уведомления через функцию sendNotification()');