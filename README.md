# KvadraAccel: система обработки данных акселерометра

`KvadraAccel` — Linux-проект на C++20, реализующий трехузловую систему обработки
данных акселерометра. Узел A эмулирует поток измерений, сервер отбрасывает
последовательные дубликаты и пересылает пакеты, узел B считает модуль вектора,
а сервер возвращает результат узлу A для записи в лог.

```text
Эмулятор сенсора Node A
  -> AccelPacket { version, timestamp, x, y, z }
  -> фильтр дубликатов на сервере
  -> расчет модуля вектора на Node B
  -> AccelModule { version, timestamp, module }
  -> обратная пересылка через сервер
  -> лог модулей на Node A
```

Дополнительная схема потока данных и привязка к уровням задания описаны в
`PIPELINE.md`.

## Возможности

- Уровень 1: TCP-рантайм с JSON-сообщениями, разделенными переводом строки.
- Опциональный асинхронный TCP/TLS/mTLS-рантайм на Boost.Asio.
- Уровень 2: gRPC/Protocol Buffers с синхронными bidirectional streams.
- Дополнительный gRPC-рантайм на C++ Callback API с `ServerBidiReactor` и
  `ClientBidiReactor`.
- Уровень 3: TLS/mTLS и прикладная проверка API-ключа.
- Автоматическое переподключение Node A и Node B после сетевых ошибок.
- JSON-конфигурация с переопределениями через командную строку.
- Unit-тесты на доменную логику, конфигурацию, CLI, JSON-протокол, очереди,
  проверку API-ключа и protobuf-конвертацию в gRPC-сборке.
- CTest-интеграционные проверки для TCP pipeline и защищенного gRPC-режима.

## Исполняемые файлы

| Файл | Назначение |
| --- | --- |
| `KvadraAccel_server` | TCP/JSON-сервер уровня 1 для подключений Node A и Node B. |
| `KvadraAccel_node_a` | Эмулятор сенсора и запись полученных модулей в лог. |
| `KvadraAccel_node_b` | Расчет модуля вектора для входящих пакетов акселерометра. |
| `KvadraAccel_grpc_server` | gRPC-сервер уровня 2 на синхронных bidirectional streams. |
| `KvadraAccel_grpc_node_a` | gRPC-клиент Node A. |
| `KvadraAccel_grpc_node_b` | gRPC-клиент Node B. |
| `KvadraAccel_grpc_callback_server` | gRPC-сервер уровня 2 на callback/reactor API. |
| `KvadraAccel_grpc_callback_node_a` | gRPC-клиент Node A на callback API. |
| `KvadraAccel_grpc_callback_node_b` | gRPC-клиент Node B на callback API. |
| `KvadraAccel_tests` | Catch2-тесты; собираются при `KVADRA_ACCEL_BUILD_TESTS=ON`. |

gRPC-исполняемые файлы собираются только с `KVADRA_ACCEL_BUILD_GRPC=ON`.

## Зависимости

Минимально нужны CMake 3.20+, компилятор C++20 и pthreads. `nlohmann/json` и
Catch2 подтягиваются через CMake `FetchContent`.

Для асинхронного TCP/TLS-рантайма нужны заголовки Boost.Asio и OpenSSL. На
Ubuntu это обычно:

```bash
sudo apt-get install libboost-dev libssl-dev
```

Если Boost.Asio недоступен, CMake выводит предупреждение и собирает POSIX
TCP-реализацию без TLS.

Для gRPC-сборки на Ubuntu:

```bash
sudo apt-get install libgrpc++-dev protobuf-compiler-grpc
```

## Сборка и тесты

Команды ниже предполагают запуск из каталога, в котором лежит папка `kvadra-accel`
например из `/home/flexmatik/impulse`.

Обычная сборка включает TCP/JSON-исполняемые файлы и тесты:

```bash
cmake -S kvadra-accel -B kvadra-accel/build
cmake --build kvadra-accel/build
ctest --test-dir kvadra-accel/build --output-on-failure
```

Полезные CMake-опции:

| Опция | Значение по умолчанию | Назначение |
| --- | --- | --- |
| `KVADRA_ACCEL_BUILD_ASIO_TCP` | `ON` | Использовать Boost.Asio TCP/TLS при наличии зависимостей. |
| `KVADRA_ACCEL_BUILD_GRPC` | `OFF` | Собрать gRPC/protobuf-рантаймы уровня 2. |
| `KVADRA_ACCEL_BUILD_TESTS` | `ON` | Собрать Catch2-тесты и зарегистрировать CTest-сценарии. |

Сборка без Boost.Asio-рантайма:

```bash
cmake -S kvadra-accel -B kvadra-accel/build -DKVADRA_ACCEL_BUILD_ASIO_TCP=OFF
cmake --build kvadra-accel/build
```

Сборка с gRPC:

```bash
cmake -S kvadra-accel -B kvadra-accel/build-grpc -DKVADRA_ACCEL_BUILD_GRPC=ON
cmake --build kvadra-accel/build-grpc
ctest --test-dir kvadra-accel/build-grpc --output-on-failure
```

Также есть готовые sanitizer-сценарии:

```bash
./kvadra-accel/asan_ubsan.sh
./kvadra-accel/tsan.sh
```

Они создают отдельные каталоги `build-asan-ubsan` и `build-tsan`, собирают
проект без gRPC и запускают CTest.

## Запуск TCP/JSON уровня 1

Если путь к конфигу не передан, все `main`-файлы используют `config/local.json`.
В примерах ниже явно используется `config/demo.json`, где `max_samples` равен
`100`, поэтому Node A завершится после генерации 100 измерений.

Запустите три процесса из каталога над `kvadra-accel`:

```bash
./kvadra-accel/build/KvadraAccel_server kvadra-accel/config/demo.json
./kvadra-accel/build/KvadraAccel_node_b kvadra-accel/config/demo.json
./kvadra-accel/build/KvadraAccel_node_a kvadra-accel/config/demo.json
```

`KvadraAccel_node_a` останавливается после `max_samples`, если это значение не
равно нулю. Для непрерывного режима установите `max_samples` в `0`; так уже
настроен `config/local.json`. Полученные модули дописываются в файл из
`module_log_path`, по умолчанию `accel/module.log`.

Параметры командной строки переопределяют значения из JSON:

```bash
./kvadra-accel/build/KvadraAccel_server kvadra-accel/config/demo.json --node-a-port 5101 --node-b-port 5102
./kvadra-accel/build/KvadraAccel_node_a kvadra-accel/config/demo.json --sensor-hz 100 --max-samples 200
```

## Запуск gRPC уровня 2

Соберите проект с `KVADRA_ACCEL_BUILD_GRPC=ON`, затем запустите синхронный streaming
рантайм:

```bash
./kvadra-accel/build-grpc/KvadraAccel_grpc_server kvadra-accel/config/demo.json
./kvadra-accel/build-grpc/KvadraAccel_grpc_node_b kvadra-accel/config/demo.json
./kvadra-accel/build-grpc/KvadraAccel_grpc_node_a kvadra-accel/config/demo.json
```

Callback API-рантайм использует тот же конфиг и тот же protobuf-протокол:

```bash
./kvadra-accel/build-grpc/KvadraAccel_grpc_callback_server kvadra-accel/config/demo.json
./kvadra-accel/build-grpc/KvadraAccel_grpc_callback_node_b kvadra-accel/config/demo.json
./kvadra-accel/build-grpc/KvadraAccel_grpc_callback_node_a kvadra-accel/config/demo.json
```

Схема находится в `proto/accelerometer.proto`. В ней описаны `StreamAccelData`
для Node A и `ProcessAccelData` для Node B, чтобы сервер мог держать отдельные
двунаправленные потоки для каждой роли.

## Защищенный режим

Сгенерируйте локальный CA, серверный и клиентский сертификаты:

```bash
cd kvadra-accel
./scripts/generate_certs.sh certs
```

После gRPC-сборки защищенный конфиг можно запустить из каталога `kvadra-accel`:

```bash
./build-grpc/KvadraAccel_grpc_server config/secure-grpc.json
./build-grpc/KvadraAccel_grpc_node_b config/secure-grpc.json
./build-grpc/KvadraAccel_grpc_node_a config/secure-grpc.json
```

`config/secure-grpc.json` включает `grpc_tls` и `grpc_mutual_tls`. Сервер
загружает `server.crt` и `server.key`, проверяет клиентские сертификаты через
`ca.crt` и дополнительно проверяет API-ключ из gRPC metadata `x-api-key`.
Клиенты проверяют серверный сертификат через `ca.crt` и отправляют
`client.crt`/`client.key`, когда включен mTLS.

Те же сертификаты используются асинхронным TCP-рантаймом, если Boost.Asio и
OpenSSL доступны:

```bash
./build/KvadraAccel_server config/secure-grpc.json --tcp-tls true --tcp-mtls true
./build/KvadraAccel_node_b config/secure-grpc.json --tcp-tls true --tcp-mtls true
./build/KvadraAccel_node_a config/secure-grpc.json --tcp-tls true --tcp-mtls true
```

## Конфигурация

Примеры конфигов:

| Файл | Назначение |
| --- | --- |
| `config/demo.json` | Демо-запуск: `sensor_hz=20`, `max_samples=100`, TLS отключен. |
| `config/local.json` | Локальный режим по умолчанию: `sensor_hz=50`, `max_samples=0`. |
| `config/secure-grpc.json` | TLS/mTLS включены для gRPC и TCP. |

Поля JSON:

| Поле | Описание |
| --- | --- |
| `server_host` | Хост, к которому подключаются клиенты. |
| `node_a_port` | TCP-порт для подключения Node A уровня 1. |
| `node_b_port` | TCP-порт для подключения Node B уровня 1. |
| `grpc_port` | Порт gRPC-сервера. |
| `api_key` | Общий прикладной ключ для TCP `hello` и gRPC metadata. |
| `sensor_hz` | Частота генерации измерений Node A. |
| `reconnect_delay_ms` | Пауза перед повторной попыткой подключения клиента. |
| `duplicate_precision` | Точность округления при сравнении последовательных дубликатов; задается только в JSON. |
| `module_log_path` | Файл для модулей, полученных Node A. |
| `max_samples` | Количество измерений Node A; `0` означает непрерывную работу. |
| `grpc_tls`, `grpc_mutual_tls` | Включение gRPC TLS и проверки клиентского сертификата. |
| `tcp_tls`, `tcp_mutual_tls` | Включение TCP TLS/mTLS в Boost.Asio-рантайме. |
| `tls_target_name` | Ожидаемое имя сертификата; локальные сертификаты создаются для `localhost`. |
| `ca_cert_path`, `server_cert_path`, `server_key_path` | Пути к CA и серверному сертификату/ключу. |
| `client_cert_path`, `client_key_path` | Пути к клиентскому сертификату/ключу для mTLS. |

Поддерживаемые CLI-переопределения: `--config`, `--host`, `--node-a-port`,
`--node-b-port`, `--grpc-port`, `--api-key`, `--sensor-hz`,
`--reconnect-delay-ms`, `--module-log`, `--max-samples`, `--grpc-tls`,
`--grpc-mtls`, `--tcp-tls`, `--tcp-mtls`, `--tls-target-name`, `--ca-cert`,
`--server-cert`, `--server-key`, `--client-cert` и `--client-key`.

## Структура проекта

```text
config/       Примеры JSON-конфигурации.
proto/        Схема gRPC-сервиса и сообщений.
scripts/      Скрипты для генерации локальных сертификатов.
src/apps/     Тонкие точки входа исполняемых файлов.
src/common/   Конфиг, CLI, доменная модель, JSON-протокол, логирование, очереди, сокеты.
src/tcp/      TCP/JSON-рантаймы уровня 1.
src/grpc/     gRPC-рантаймы уровня 2 и protobuf-конвертация.
tests/        Catch2 unit-тесты и shell-интеграционные тесты.
PIPELINE.md   Описание потока данных и уровней задания.
```

Общий код находится в namespace `accel`. `main`-файлы загружают
`accel::AppConfig`, создают соответствующий app-класс и вызывают `Run()`.

Ключевые RAII и доменные классы:

- `accel::SocketFd`, `accel::TcpListener` и `accel::TcpConnection` владеют
  POSIX-сокетами.
- `accel::ModuleLogWriter` владеет путем к логу модулей и создает родительские
  каталоги перед записью.
- `accel::BlockingQueue<T>` синхронизирует состояние пересылки с
  детерминированными пробуждениями.
- `accel::DuplicateFilter`, `accel::SensorEmulator` и
  `accel::ModuleCalculator` не зависят от сети и покрыты unit-тестами.

## Тестовое покрытие

`KvadraAccel_tests` включает проверки:

- `blocking_queue_test.cpp` — FIFO, таймауты и закрытие очереди.
- `cli_test.cpp` — разбор CLI и переопределения конфигурации.
- `config_test.cpp` — загрузка JSON и валидация портов, API-ключа и TLS-настроек.
- `domain_test.cpp` — расчет модуля, фильтр дубликатов, эмулятор сенсора и время.
- `json_protocol_test.cpp` — TCP `hello`, пакеты, модули, API-ключ и ошибки парсинга.
- `proto_conversion_test.cpp` — protobuf-конвертация, только при `KVADRA_ACCEL_BUILD_GRPC=ON`.

CTest дополнительно регистрирует `e2e_level1_pipeline` из
`tests/e2e_level1_test.sh`, а в gRPC-сборке еще и `integration_grpc_security` из
`tests/integration_grpc_security_test.sh`.

## Протокол и обработка отказов

Уровень 1 использует JSON, разделенный переводом строки, чтобы сообщения было
легко смотреть обычными TCP-инструментами. Каждый клиент сначала отправляет
`hello` с ролью и API-ключом. Уровень 2 использует Protocol Buffers и gRPC,
потому что типизированная схема и bidirectional streams соответствуют контракту
задания.

Node A и Node B считают потерю соединения нормальной runtime-ситуацией:
закрывают текущее подключение, ждут `reconnect_delay_ms` и подключаются заново.
Сервер после отключения клиента продолжает слушать порт. Ошибки запуска и
конфигурации считаются фатальными и выводятся с понятным сообщением.

## Android и SELinux

Текущая реализация ориентирована на Linux-only вариант задания. Node A
эмулирует поток акселерометра на C++ и не требует доступа к Android Sensor NDK.
Если переносить ядро на Android, практичнее сделать небольшой Java/Kotlin
foreground service, загрузить native library и вызывать C++ через JNI. Прямой
запуск native binaries через `adb shell` может упереться в ограничения SELinux;
для экспериментов в эмуляторе в задании также допустимы root плюс
`setenforce 0` или чистая эмуляция сенсора, как реализовано здесь.
