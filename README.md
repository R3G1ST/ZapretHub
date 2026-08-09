<div align="center">

<h1>ZapretHub</h1>

<p><b>GUI менеджер для обхода DPI-блокировок</b></p>

[![License](https://img.shields.io/badge/лицензия-GPL_3.0-blue?style=flat-square)](LICENSE)
[![Version](https://img.shields.io/badge/версия-1.9.0-green?style=flat-square)](https://github.com/R3G1ST/ZapretHub/releases/latest)

</div>

---

<p align="center">
  <a href="#-rus">Русский</a> • <a href="#-english">English</a>
</p>

---

# 🇷🇺 Русский

## О проекте

**ZapretHub** — это GUI приложение для управления DPI-обходом на Windows. Поддерживает 4 компонента:

| Компонент | Описание | Репозиторий |
|-----------|----------|-------------|
| **Zapret** | Основной DPI bypass | [Flowseal/zapret-discord-youtube](https://github.com/Flowseal/zapret-discord-youtube) |
| **Zapret 2** | Расширенная версия | [bol-van/zapret2](https://github.com/bol-van/zapret2) |
| **TgWsProxy** | WebSocket прокси | [Flowseal/tg-ws-proxy](https://github.com/Flowseal/tg-ws-proxy) |
| **Veto** | Собственный DPI движок | [R3G1ST/Veto](https://github.com/R3G1ST/Veto) |

## Возможности

- Автоматическая диагностика и настройка
- Управление стратегиями обхода (.bat файлы)
- Управление списками доменов (835+ доменов, 10 категорий)
- Импорт/экспорт модов (.zip)
- Проверка обновлений через GitHub API
- Трей-иконка и автозапуск
- Discord Rich Presence
- Встроенная диагностика Telegram, Discord, DNS, DPI

## Архитектура (v1.9.0+)

Модульная структура проекта:

| Проект | Назначение |
|--------|------------|
| **ZapretHub.Core** | Общие сервисы (обновления, уведомления, настройки, моды) |
| **ZapretHub.Zapret** | Модуль Zapret (diagnostics, config) |
| **ZapretHub.Veto** | Модуль Veto (VetoService) |
| **ZapretHub.TgWsProxy** | Модуль TgWsProxy (настройки) |
| **ZapretHub** | UI точка входа (MainWindow, Views) |

## Скачивание

Скачайте последнюю версию с [GitHub Releases](https://github.com/R3G1ST/ZapretHub/releases/latest).

### Системные требования

- Windows 10/11 (x64)
- Запуск от имени администратора

## Сборка

```bash
dotnet publish -c Release -r win-x64 --self-contained true /p:PublishSingleFile=true
```

## Лицензия

GPL-3.0

---

# 🇬🇧 English

## About

**ZapretHub** is a GUI manager for DPI bypass on Windows. Supports 4 components:

| Component | Description | Repository |
|-----------|-------------|------------|
| **Zapret** | Main DPI bypass | [Flowseal/zapret-discord-youtube](https://github.com/Flowseal/zapret-discord-youtube) |
| **Zapret 2** | Extended version | [bol-van/zapret2](https://github.com/bol-van/zapret2) |
| **TgWsProxy** | WebSocket proxy | [Flowseal/tg-ws-proxy](https://github.com/Flowseal/tg-ws-proxy) |
| **Veto** | Custom DPI engine | [R3G1ST/Veto](https://github.com/R3G1ST/Veto) |

## Features

- Automatic diagnostics and setup
- Bypass strategy management (.bat files)
- Domain list management (835+ domains, 10 categories)
- Mod import/export (.zip)
- Update checking via GitHub API
- System tray and autostart
- Discord Rich Presence
- Built-in diagnostics for Telegram, Discord, DNS, DPI

## Architecture (v1.9.0+)

Modular project structure:

| Project | Purpose |
|---------|---------|
| **ZapretHub.Core** | Shared services (updates, notifications, settings, mods) |
| **ZapretHub.Zapret** | Zapret module (diagnostics, config) |
| **ZapretHub.Veto** | Veto module (VetoService) |
| **ZapretHub.TgWsProxy** | TgWsProxy module (settings) |
| **ZapretHub** | UI entry point (MainWindow, Views) |

## Download

Download the latest version from [GitHub Releases](https://github.com/R3G1ST/ZapretHub/releases/latest).

### System Requirements

- Windows 10/11 (x64)
- Run as administrator

## Building

```bash
dotnet publish -c Release -r win-x64 --self-contained true /p:PublishSingleFile=true
```

## License

GPL-3.0

---

<p align="center">
  <sub>Made by <a href="https://github.com/R3G1ST">R3G1ST</a></sub>
</p>
