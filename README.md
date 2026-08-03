<div align="center">

<h1>ZapretHub</h1>

<p><b>GUI менеджер Zapret и tg-ws-proxy для обхода блокировок</b></p>

[![License](https://img.shields.io/badge/лицензия-GPL_3.0-blue?style=flat-square)](LICENSE)

</div>

---

## О проекте

**ZapretHub** — это GUI приложение для управления [zapret-discord-youtube](https://github.com/Flowseal/zapret-discord-youtube) и [tg-ws-proxy](https://github.com/Flowseal/tg-ws-proxy). Позволяет обходить блокировки сайтов и сервисов на Windows без VPN.

Возможности:
- Автоматическая диагностика и настройка Zapret
- Управление стратегиями обхода (.bat файлы)
- Управление списками доменов
- Импорт/экспорт модов (.zip)
- Создание модов из .bat файлов
- Автообновление компонентов
- Трей-иконка и автозапуск

---

## Скачивание

Скачайте последнюю версию с [GitHub Releases](https://github.com/R3G1ST/ZapretHub/releases/latest).

### Системные требования

- Windows 10/11 (x64)
- Запуск от имени администратора
- [.NET Desktop Runtime 8.0](https://dotnet.microsoft.com/download/dotnet/8.0)

---

## Как это работает

1. Запустите приложение от имени администратора
2. Нажмите кнопку запуска — программа проверит и настроит компоненты
3. Заблокированные сайты станут доступны

---

## Сборка

```bash
git clone https://github.com/R3G1ST/ZapretHub.git
```

1. Откройте `ZapretHub.sln` в Visual Studio
2. Нажмите **Сборка → Собрать решение**

### Компоненты

Сторонние компоненты (Zapret, TgWsProxy) скачиваются автоматически из официальных репозиториев:
- [Flowseal/zapret-discord-youtube](https://github.com/Flowseal/zapret-discord-youtube)
- [Flowseal/tg-ws-proxy](https://github.com/Flowseal/tg-ws-proxy)

---

## Авторы компонентов

- **Zapret** — [Flowseal](https://github.com/Flowseal)
- **tg-ws-proxy** — [Flowseal](https://github.com/Flowseal)

---

## Лицензия

Проект распространяется под лицензией [GPL-3.0](LICENSE).

**Отказ от ответственности:** Программа предоставляется «как есть». Используя ZapretHub, вы подтверждаете, что делаете это на свой страх и риск.

---

<div align="center">
  <sub>v1.1.8 · 2026</sub>
</div>
