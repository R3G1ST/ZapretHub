# Veto - DPI Bypass Engine

Собственный движок для обхода DPI (Deep Packet Inspection), альтернатива zapret/nfqws.

## Архитектура

```
veto/
├── include/           # Заголовочные файлы
│   ├── veto.h        # Основные типы и константы
│   ├── packet.h      # Парсинг пакетов
│   ├── tcp_reassembly.h  # Сборка TCP потоков
│   ├── proto_detect.h    # Определение протоколов
│   ├── attacks.h     # Модули атак
│   ├── capture.h     # Перехват через WinDivert
│   ├── config.h      # Конфигурация
│   └── veto_engine.h # Основной движок
├── src/
│   ├── core/
│   │   ├── packet.c
│   │   ├── tcp_reassembly.c
│   │   └── veto_engine.c
│   ├── protocols/
│   │   └── proto_detect.c
│   ├── attacks/
│   │   └── attacks.c
│   ├── capture/
│   │   └── capture.c
│   ├── config/
│   │   └── config.c
│   └── main.c
├── lib/               # WinDivert библиотека
├── bin/               # Собранный бинарник
├── CMakeLists.txt     # CMake конфигурация
├── build.bat          # Сборка MSVC
├── build-gcc.bat      # Сборка GCC/MinGW
├── veto.conf          # Пример конфигурации
└── download-windivert.bat  # Скачивание WinDivert
```

## Сборка

### 1. Скачать WinDivert
```bash
download-windivert.bat
```

### 2. Собрать
```bash
build-gcc.bat
```

### 3. Запустить
```bash
bin\veto.exe --attack fake --hostlist files/list-youtube.txt
```

## Модули

### Packet Parser (`packet.c`)
- Парсинг IPv4/TCP/UDP заголовков
- Вычисление контрольных сумм
- Извлечение payload

### TCP Reassembly (`tcp_reassembly.c`)
- Сборка TCP потоков из фрагментов
- Отслеживание_seq/ack
- Обработка SYN/FIN/RST

### Protocol Detection (`proto_detect.c`)
- TLS ClientHello с извлечением SNI
- HTTP запросы/ответы с Host
- QUIC, WireGuard, Discord

### Attack Modules (`attacks.c`)
- **FAKE** — отправка поддельных пакетов с невалидной TTL
- **SPLIT** — разбивка TCP сегментов
- **DISORDER** — отправка сегментов в неправильном порядке
- **FAKED_SPLIT** — комбинация fake + split

### Capture Layer (`capture.c`)
- Перехват через WinDivert
- Фильтрация по портам (80, 443)
- Инъекция модифицированных пакетов

## Конфигурация

Пример `veto.conf`:
```ini
[general]
max_streams=4096
timeout_ms=30000

[profile:youtube]
enabled=true

  [strategy:fake]
  type=fake
  fool=ttl
  fake_ttl=4
  hostlist=files/list-youtube.txt
```

## Отличия от zapret

| | Veto | zapret/nfqws |
|---|---|---|
| Язык | C (чистый) | C + Lua (zapret2) |
| Размер | ~2000 строк | ~30000 строк |
| Модульность | Полная модульность | Монолитный |
| Lua | Пока нет | Есть в zapret2 |

## Статус

- [x] Архитектура
- [x] Packet parser
- [x] TCP reassembly
- [x] Protocol detection
- [x] Attack modules
- [x] Config system
- [x] CLI
- [ ] WinDivert integration (нужна библиотека)
- [ ] Тестирование
- [ ] Lua scripting
- [ ] Интеграция с ZapretHub
