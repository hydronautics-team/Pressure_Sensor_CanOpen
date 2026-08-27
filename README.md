# Датчик давления STM32F103 с CANopen (CiA 404)

Прошивка измеряет давление через АЦП ADS1220, рассчитывает глубину и публикует результаты в сети CANopen. Устройство работает как CANopen slave с фиксированными параметрами:

| Параметр | Значение |
|---|---:|
| Микроконтроллер | STM32F103C8T6 |
| CANopen Node-ID | `3` |
| Скорость CAN | `250 кбит/с` |
| CAN-фреймы | Classic CAN, стандартный 11-битный ID |
| Heartbeat | `1000 мс` |
| TPDO | TPDO1, COB-ID `0x183`, 8 байт |
| SDO request/response | `0x603` / `0x583` |
| Профиль измерений | базовый набор CiA 404-1 |
| Стек | [CANoopEn 3.1.10](https://github.com/xyntos-ch/CANoopEn/releases/tag/3.1.10) |

Это базовая совместимая реализация CiA 404, но она не заявляет прохождение официального CANopen conformance test.

## Что происходит после включения

1. Инициализируются тактирование, GPIO, SPI1, CAN1 и ADS1220.
2. CANopen-узел отправляет boot-up кадр `0x703 [00]`.
3. Узел переходит в состояние NMT Pre-operational и раз в секунду отправляет heartbeat `0x703 [7D]`.
4. В Pre-operational уже доступен SDO-сервер, поэтому мастер может читать object dictionary.
5. Измерения обновляются внутри object dictionary, но TPDO ещё не передаётся.
6. Мастер отправляет NMT Start: `0x000 [01 03]`.
7. Узел переходит в Operational и начинает отправлять TPDO1 после каждой корректной выборки ADS1220 — примерно 20 раз в секунду.

Первая корректная выборка после включения сохраняется как атмосферная опора. Поэтому первая рассчитанная глубина равна `0 мм`.

## Архитектура проекта

```text
ADS1220
   │ raw ADC
   ▼
PressureProcessor ──► pressure, depth
   │
   ▼
PressureSensorCanopen ──► Object Dictionary ──► CANoopEn
                                                   │
                                                   ▼
                                    CanDriver ──► STM32 bxCAN ──► CAN bus
```

Основные файлы:

| Файл | Назначение |
|---|---|
| `Core/Src/main.cpp` | Инициализация платы и основной цикл |
| `Core/Src/ADS1220.cpp` | Обмен с внешним АЦП |
| `Core/Inc/pressure_processing.hpp` | Пересчёт кода АЦП в давление и глубину |
| `Core/Src/CANopen/PressureSensorCanopen.cpp` | Прикладной CANopen-узел, NMT и публикация измерений |
| `Core/Src/CANopen/CanDriver.cpp` | Драйвер STM32F103 bxCAN и очереди RX/TX |
| `Core/Inc/CANopen/CoSettings.hpp` | Ограничения памяти и включённые возможности стека |
| `EDS/PressureSensor.eds` | Главный редактируемый источник object dictionary |
| `Core/Inc/CANopen/PressureSensorOd.hpp` | Сгенерированное объявление object dictionary |
| `Core/Src/CANopen/PressureSensorOd.cpp` | Сгенерированная реализация object dictionary |
| `Drivers/CANoopEn` | Git submodule со стеком CANoopEn |

CANoopEn подключается без изменений внутри submodule. Нужные исходники, include paths и отключённые возможности задаются локально в корневом `CMakeLists.txt`.

## Как работает прошивка

### Запуск устройства

Точка входа находится в `Core/Src/main.cpp`. Последовательность запуска следующая:

1. `HAL_Init()` настраивает HAL и системный таймер STM32.
2. `SystemClock_Config()` поднимает частоту ядра до 72 МГц и PCLK1 до 36 МГц.
3. Инициализируются GPIO, SPI1 и CAN1.
4. `externalAdc.Init()` настраивает ADS1220.
5. `canopenNode.Init()` подключает к CANoopEn источник времени `HAL_GetTick()`, запускает CAN-драйвер и CANopen slave.
6. После успешной инициализации устанавливается сигнал `STM_ALIVE`.
7. Управление остаётся в бесконечном основном цикле; операционная система и потоки не используются.

Если ADS1220 или CAN не удалось запустить, вызывается `Error_Handler()` и основной цикл не начинается.

### Основной цикл и измерение

На каждой итерации `while (1)` выполняются две независимые задачи:

```text
canopenNode.Proceed()       обработать CAN и таймеры CANopen
externalAdc.ReadMeasurement()
        │
        ├── данных ещё нет ──► продолжить ожидание
        ├── ошибка SPI ──────► MarkMeasurementInvalid()
        └── готовый код АЦП ─► PressureProcessor::Process()
                                      │
                                      ├── ошибка ─► MarkMeasurementInvalid()
                                      └── pressure/depth ─► PublishMeasurement()
```

Когда преобразование закончено, прерывание `EXTI3_IRQHandler` сообщает драйверу ADS1220 о спаде сигнала DRDY. Чтение SPI выполняется из основного цикла, а не внутри обработчика прерывания. После чтения немедленно запускается следующая выборка.

`PressureProcessor` выполняет целочисленный пересчёт:

```text
код ADS1220 ─► напряжение, мВ ─► абсолютное давление, Па ─► глубина, мм
```

Используется модель датчика 0,5–4,5 В с диапазоном 0–300 PSI. Первая корректная величина давления сохраняется как атмосферная опора. Глубина рассчитывается по избыточному давлению; если текущее давление не выше опорного, результат равен нулю.

### Публикация измерения

`PressureSensorCanopen::PublishMeasurement()` делает четыре записи в object dictionary:

```cpp
0x9100:01 = rawAdc;
0x9130:01 = pressurePa;
0x2000:00 = depthMm;
0x6150:01 bit 0 = 0;
```

После этого `WritePdoAsync(1)` просит CANoopEn сформировать TPDO по mapping из `0x1A00`. Вызов выполняется только при `operational_ == true`. Этот флаг обновляется callback-функцией `OnNmtStateChange()` после команды NMT Start/Stop/Pre-operational.

`MarkMeasurementInvalid()` устанавливает bit 0 объекта `0x6150:01`. Давление и глубина при этом не перезаписываются, поэтому через SDO остаются доступны последние корректные значения. TPDO для ошибочной выборки не формируется.

### Приём и передача CAN

Аппаратные обработчики CAN вызывают STM32 HAL, а `CanDriver` переносит кадры между прерываниями и основным циклом через две кольцевые очереди по 16 кадров:

```text
CAN RX interrupt ─► RX queue ─► PressureSensorCanopen::Proceed() ─► CANoopEn
CANoopEn ─► TX queue ─► свободный bxCAN mailbox ─► CAN TX interrupt
```

Фильтр принимает только стандартные 11-битные CAN-ID. Extended CAN и кадры длиннее восьми байт отбрасываются. При переполнении увеличивается внутренний счётчик `droppedRxFrames_` или `droppedTxFrames_`; в текущей версии эти счётчики не опубликованы в OD.

`PressureSensorCanopen::Proceed()`:

- передаёт все принятые кадры стеку;
- локально преобразует стандартную NMT-команду Pre-operational `0x80` в значение, ожидаемое CANoopEn 3.1.10;
- каждые 10 мс запускает периодическую обработку NMT, heartbeat, SDO и таймаутов.

### Callback-функции CANoopEn

Класс `PressureSensorCanopen` реализует несколько обязательных интерфейсов стека:

| Метод | Когда вызывается | Что делает сейчас |
|---|---|---|
| `SendCanMessage()` | Стек сформировал CAN-кадр | Передаёт кадр в TX-очередь драйвера |
| `OnNmtStateChange()` | Изменилось NMT-состояние | Разрешает или запрещает отправку TPDO |
| `OnValueChanged()` | Объект был изменён через SDO/PDO | Ничего; аргументы подавляют предупреждения компилятора |
| `OnSdoAbort()` | SDO-транзакция завершилась abort | Ничего |
| `OnNmtCommand()` | Получена NMT-команда | Ничего; состояние обрабатывается в `OnNmtStateChange()` |
| `OnHeartbeatTimeout()` | Истёк heartbeat удалённого узла | Ничего, так как heartbeat consumer не используется приложением |
| `OnDebugOutput()` | Стек выводит диагностическое сообщение | Ничего; UART-логирование не подключено |

Пустые callback-функции всё равно нужны: они объявлены как pure virtual в интерфейсах CANoopEn. Без их реализации `PressureSensorCanopen` был бы абстрактным классом.

## Коротко о CANopen

CANopen использует обычные CAN-кадры, но заранее определяет назначение идентификаторов и формат данных.

- **NMT** управляет состоянием узла: Start, Stop, Pre-operational и Reset.
- **SDO** читает или записывает отдельный объект по паре `index:sub-index`.
- **PDO** быстро передаёт заранее настроенный набор процессных данных без индексов и служебных полей.
- **Heartbeat** сообщает, что узел жив, и показывает его текущее NMT-состояние.
- **Object Dictionary (OD)** — таблица всех доступных параметров и измерений устройства.
- **EDS** — текстовое описание OD для CANopen-конфигураторов и генераторов кода.

Устройство поддерживает NMT, heartbeat producer, SDO server и один TPDO. В этой версии отсутствуют RPDO, SYNC, TIME, EMCY, LSS, MPDO и SDO block transfer.

### CAN-идентификаторы

| CAN-ID | Направление | Назначение |
|---:|---|---|
| `0x000` | мастер → узел | NMT-команда |
| `0x183` | узел → сеть | TPDO1: давление и глубина |
| `0x603` | клиент → узел | SDO request для Node-ID 3 |
| `0x583` | узел → клиент | SDO response для Node-ID 3 |
| `0x703` | узел → сеть | boot-up и heartbeat |

Node-ID мастера не влияет на приоритет NMT: приоритет CAN определяется идентификатором кадра. NMT имеет CAN-ID `0x000`, то есть наивысший приоритет среди используемых здесь сообщений.

### NMT-команды

NMT-кадр содержит два байта: код команды и Node-ID. Node-ID `0` адресует все узлы.

| Действие | Данные для узла 3 | Heartbeat после команды |
|---|---|---|
| Start | `01 03` | `05` — Operational |
| Stop | `02 03` | `04` — Stopped |
| Pre-operational | `80 03` | `7D` — Pre-operational |
| Reset node | `81 03` | новый boot-up `00`, затем Pre-operational |
| Reset communication | `82 03` | новый boot-up `00`, затем Pre-operational |

### Формат TPDO1

TPDO1 передаётся с CAN-ID `0x183`, DLC `8`. Все многобайтные числа передаются в little-endian порядке: младший байт первым.

| Байты | OD-объект | Тип | Значение |
|---|---|---|---|
| `0..3` | `0x9130:01` | `INTEGER32` | абсолютное давление, Па |
| `4..7` | `0x2000:00` | `UNSIGNED32` | глубина, мм |

Пример: давление `101325 Па` (`0x00018BCD`) и глубина `1234 мм` (`0x000004D2`) передаются так:

```text
CAN-ID  DLC  DATA
0x183   8    CD 8B 01 00 D2 04 00 00
```

TPDO отправляется только при двух условиях:

1. новая выборка успешно обработана;
2. узел находится в NMT Operational.

При ошибке измерения выставляется `0x6150:01`, bit 0 — `Not valid`. Последние корректные давление и глубина остаются в OD, новый TPDO не отправляется.

## Object Dictionary и EDS

EDS находится в `EDS/PressureSensor.eds`. Это обычный текстовый INI-подобный файл стандарта CiA 306. Его можно открыть любым текстовым редактором или EDS-редактором.

Важно: **EDS в этом проекте не генерируется из C++**. Наоборот, EDS является исходником, из которого официальная утилита Eds2Od генерирует C++ object dictionary.

```text
PressureSensor.eds
        │ Eds2Od
        ├──► PressureSensorOd.hpp
        └──► PressureSensorOd.cpp
```

В EDS указаны только 250 кбит/с, один TPDO, отсутствие RPDO и отсутствие LSS.

### Как читать структуру EDS

Каждый раздел начинается именем в квадратных скобках, а параметры записываются как `ключ=значение`. Комментарии начинаются с `;`. Регистр шестнадцатеричных чисел не важен, но в проекте используется запись с префиксом `0x`.

Файл состоит из трёх смысловых частей:

1. сведения о файле и устройстве;
2. списки поддерживаемых объектов;
3. описание каждого объекта и его подындексов.

Основные служебные секции:

| Секция | Что в ней находится |
|---|---|
| `[FileInfo]` | Имя, версия, дата создания и описание самого EDS-файла |
| `[DeviceInfo]` | Производитель, код продукта, ревизия и заявленные возможности CANopen |
| `[DummyUsage]` | Использование dummy-типов для PDO mapping; здесь всё отключено |
| `[Comments]` | Пояснение назначения устройства для человека |
| `[MandatoryObjects]` | Обязательные объекты CiA 301 |
| `[OptionalObjects]` | Использованные стандартные и профильные объекты |
| `[ManufacturerObjects]` | Пользовательские объекты диапазона `0x2000–0x5FFF` |

В `[DeviceInfo]` строки `BaudRate_250=1`, `NrOfTXPDO=1`, `NrOfRXPDO=0` и `LSS_Supported=0` сообщают конфигуратору возможности устройства. Они не изменяют регистры STM32 и не включают модули CANoopEn при сборке.

### Поля одного объекта

Простой объект описывается одной секцией. Например, глубина:

```ini
[2000]
ParameterName=Depth Millimeters
ObjectType=0x7
DataType=0x0007
AccessType=ro
DefaultValue=0
PDOMapping=1
```

| Поле | Значение |
|---|---|
| `ParameterName` | Читаемое человеком имя |
| `ObjectType=0x7` | `VAR`, то есть одиночное значение |
| `DataType=0x0007` | Тип `UNSIGNED32` |
| `AccessType=ro` | Мастер может читать, но не записывать |
| `DefaultValue=0` | Начальное значение после запуска |
| `PDOMapping=1` | Объект разрешено включать в PDO |

Использованные в проекте типы:

| `DataType` | Тип CANopen | Размер |
|---:|---|---:|
| `0x0004` | `INTEGER32` | 32 бита |
| `0x0005` | `UNSIGNED8` | 8 бит |
| `0x0006` | `UNSIGNED16` | 16 бит |
| `0x0007` | `UNSIGNED32` | 32 бита |
| `0x0009` | `VISIBLE_STRING` | строка |

Основные режимы доступа: `const` — константа, `ro` — только чтение, `rw` — чтение и запись. Сетевой доступ проверяется SDO-сервером: попытка записи в `ro` или `const` завершается SDO abort.

### RECORD и подындексы

Составной объект использует `ObjectType=0x9` (`RECORD`). Например, Identity Object:

```ini
[1018]
ParameterName=Identity Object
ObjectType=0x9
SubNumber=5

[1018sub0]
ParameterName=Number of Entries
...
DefaultValue=4

[1018sub1]
ParameterName=Vendor ID
...
```

`SubNumber=5` означает, что описаны секции от `sub0` до `sub4`. Подындекс `0` хранит число содержательных записей, а данные начинаются с подындекса `1`. В этом проекте `ObjectType=0x8` (`ARRAY`) используется для однотипных каналов CiA 404, например `0x6110` и `0x9130`; структура секций с `sub0`, `sub1` аналогична.

### Списки объектов

Каждый добавленный объект должен присутствовать ровно в одном из списков. Число `SupportedObjects` обязано совпадать с количеством строк ниже него:

```ini
[ManufacturerObjects]
SupportedObjects=2
1=0x2000
2=0x2001
```

Если добавить секцию `[2001]`, но забыть строку `2=0x2001`, конфигуратор или Eds2Od может не включить объект. Если удалить объект, нужно также удалить его из списка и перенумеровать строки без пропусков.

### Как задаётся TPDO mapping

`[1800]` описывает способ передачи TPDO1, а `[1A00]` — его полезную нагрузку:

```ini
[1800sub1]
DefaultValue=$NODEID+0x180

[1800sub2]
DefaultValue=255

[1A00sub0]
DefaultValue=2

[1A00sub1]
DefaultValue=0x91300120
```

`$NODEID` подставляется при создании OD. Для Node-ID 3 получается `0x180 + 3 = 0x183`. Transmission type `255` означает асинхронную, управляемую событием отправку; в коде таким событием является корректное новое измерение.

32-битное значение mapping кодируется так:

```text
0x IIII SS LL
   │    │  └── длина в битах
   │    └───── sub-index
   └────────── index

0x91300120 = index 0x9130, sub-index 0x01, 0x20 = 32 бита
```

Одного `PDOMapping=1` недостаточно: объект также должен быть перечислен в `0x1A00`. И наоборот, mapping не должен ссылаться на объект с `PDOMapping=0`. Суммарная длина TPDO не может превышать 64 бита. Текущий TPDO уже занимает все восемь байт, поэтому третий объект можно добавить только после удаления другого поля, уменьшения размеров или создания ещё одного TPDO с соответствующими изменениями кода и `CoSettings.hpp`.

### Что EDS настраивает, а что только описывает

EDS является источником generated object dictionary и описанием для внешних CANopen-инструментов, но не заменяет конфигурацию периферии и приложения:

| Изменение | Где менять |
|---|---|
| Начальное значение, тип или доступ OD | EDS, затем запустить Eds2Od |
| Identity | `[DeviceInfo]` и `0x1018` в EDS должны совпадать |
| Имя и версии `0x1008–0x100A` | EDS и пользовательская инициализация строк в `PressureSensorOd.cpp` |
| Node-ID | `kNodeId` в `PressureSensorCanopen.hpp`; `$NODEID` пересчитает COB-ID |
| Скорость CAN | `MX_CAN_Init()` и `pressure.ioc`; в EDS отметить ту же поддерживаемую скорость |
| Состав TPDO | `0x1A00` и `PDOMapping` в EDS, затем генерация |
| Число TPDO и лимиты стека | EDS, `CoSettings.hpp` и при необходимости CMake-настройки CANoopEn |
| Формула давления/глубины | `pressure_processing.hpp`, а не EDS |

Используемый генератор Eds2Od не заполняет буферы `VISIBLE_STRING` из `DefaultValue`. Поэтому `0x1008–0x100A` дополнительно устанавливаются в конце конструктора `PressureSensorOd`, за пределами generated-маркеров. При изменении этих строк нужно синхронно исправить EDS и эти три вызова `SetValue()`.

### Безопасные примеры настройки

Чтобы изменить heartbeat по умолчанию с 1000 на 500 мс:

1. найдите `[1017]`;
2. замените `DefaultValue=1000` на `DefaultValue=500`;
3. перегенерируйте OD и соберите прошивку;
4. проверьте по `candump`, что heartbeat приходит дважды в секунду.

Чтобы добавить новый read-only параметр `UNSIGNED16` с индексом `0x2001`, добавьте его в `[ManufacturerObjects]`, исправьте `SupportedObjects`, затем создайте секцию:

```ini
[2001]
ParameterName=Example Parameter
ObjectType=0x7
DataType=0x0006
AccessType=ro
DefaultValue=0
PDOMapping=0
```

После генерации приложение должно обновлять его вызовом `objectDictionary_.SetValue(0x2001, 0x00, value)`. Если объект должен изменяться мастером, используйте `AccessType=rw` и обработайте изменение в `OnValueChanged()`. Для параметров, требующих сохранения после выключения питания, отдельно нужна реализация Flash-хранилища: EDS сам по себе значения не сохраняет.

### Объекты связи

| Объект | Доступ | Назначение |
|---|---|---|
| `0x1000:00` | ro | Device type `0x00020194`, CiA 404 analog-input block |
| `0x1001:00` | ro | CANopen error register |
| `0x1008:00` | const | Имя устройства `SAUVC Pressure Sensor` |
| `0x1009:00` | const | Версия аппаратуры `1.0` |
| `0x100A:00` | const | Версия прошивки `1.0.0` |
| `0x1017:00` | rw | Период heartbeat, по умолчанию `1000 мс` |
| `0x1018` | ro | Identity: vendor, product, revision и serial |
| `0x1200` | ro | Параметры SDO server, COB-ID `0x603/0x583` |
| `0x1800` | ro | Параметры TPDO1, COB-ID `0x183`, transmission type `255` |
| `0x1A00` | ro | TPDO1 mapping |

### Объекты измерительного канала

| Объект | Тип | Доступ | Назначение |
|---|---|---|---|
| `0x6110:01` | `UNSIGNED16` | const | Тип датчика `90`, pressure transducer |
| `0x6131:01` | `UNSIGNED32` | const | Единица `0x00220000`, паскаль |
| `0x6132:01` | `UNSIGNED8` | const | Число десятичных разрядов `0` |
| `0x6150:01` | `UNSIGNED8` | ro | Статус давления; bit 0 — `Not valid` |
| `0x9100:01` | `INTEGER32` | ro | Исходный код ADS1220 |
| `0x9130:01` | `INTEGER32` | ro | Абсолютное давление, Па |
| `0x2000:00` | `UNSIGNED32` | ro | Глубина, мм; manufacturer-specific объект |

Mapping TPDO занимает ровно 64 бита:

```text
0x1A00:01 = 0x91300120  # index 9130, sub-index 01, длина 0x20 = 32 бита
0x1A00:02 = 0x20000020  # index 2000, sub-index 00, длина 0x20 = 32 бита
```

Состав аналогового канала следует базовой модели CiA 404, где различаются field value и process value. Практический пример mapping `0x91300120` и передачи `INTEGER32` младшим байтом вперёд приведён в руководстве CANopen-датчика Danfoss:

- [описание CiA 404](https://www.can-cia.org/can-knowledge/cia-404-canopen-device-profile-for-measuring-devices-and-closed-loop-controllers);
- [Danfoss CANopen pressure sensor operating guide](https://assets.danfoss.com/documents/latest/241223/AQ427550352216en-000201.pdf).

## Как изменить EDS и повторно сгенерировать OD

Используется официальная утилита [CANoopEnTools/Eds2Od](https://github.com/xyntos-ch/CANoopEnTools). CANoopEnTools намеренно не добавлен в этот репозиторий как ещё один submodule.

### Вариант 1: готовый Eds2Od

1. Скачайте исполняемый файл для своей ОС со страницы [CANoopEnTools Releases](https://github.com/xyntos-ch/CANoopEnTools/releases/latest).
2. На Linux выдайте разрешение на запуск:

   ```bash
   chmod +x /путь/к/Eds2Od
   ```

3. Из корня этого репозитория выполните:

   ```bash
   /путь/к/Eds2Od \
     EDS/PressureSensor.eds \
     Core/Src/CANopen/PressureSensorOd.cpp \
     Core/Inc/CANopen/PressureSensorOd.hpp
   ```

Порядок аргументов важен: сначала EDS, затем `.cpp`, затем `.hpp`.

### Вариант 2: запуск Eds2Od из исходников

Для актуальной версии CANoopEnTools требуется .NET SDK 10.

```bash
git clone https://github.com/xyntos-ch/CANoopEnTools.git ../CANoopEnTools

dotnet run --project ../CANoopEnTools/Eds2Od/Eds2Od.csproj -- \
  EDS/PressureSensor.eds \
  Core/Src/CANopen/PressureSensorOd.cpp \
  Core/Inc/CANopen/PressureSensorOd.hpp
```

### Правильный порядок изменения словаря

1. Отредактируйте `EDS/PressureSensor.eds`.
2. Проверьте `SupportedObjects`, тип данных, доступ и `PDOMapping` изменённых объектов.
3. Если изменяется TPDO, убедитесь, что суммарная длина mapping не превышает 64 бита.
4. Запустите Eds2Od.
5. Не исправляйте вручную код между маркерами:

   ```cpp
   // *** BEGIN GENERATED CODE (Eds2Od) ***
   // *** END GENERATED CODE ***
   ```

6. Пользовательский код размещайте только вне этих маркеров. Eds2Od сохраняет такие участки при повторной генерации. Так, строки `0x1008..0x100A` инициализируются вне generated-региона.
7. Если сборка сообщает, что `MaxNumberOfOdEntries` слишком мал, проверьте изменение и при необходимости увеличьте лимит в `Core/Inc/CANopen/CoSettings.hpp`.
8. Соберите Debug и Release.
9. Повторно запустите ту же команду Eds2Od. Второй запуск не должен давать содержательных изменений в сгенерированных файлах.

Полезная проверка после генерации:

```bash
git diff -- \
  EDS/PressureSensor.eds \
  Core/Src/CANopen/PressureSensorOd.cpp \
  Core/Inc/CANopen/PressureSensorOd.hpp
```

## Подготовка окружения

Ниже приведён наиболее простой вариант для Ubuntu или WSL2. Требуются:

- Git;
- CMake не ниже `3.22`;
- GNU Arm Embedded Toolchain (`arm-none-eabi-gcc/g++`);
- Make;
- OpenOCD и ST-Link для прошивки;
- CAN-адаптер для проверки протокола.

Установка пакетов в Ubuntu:

```bash
sudo apt-get update
sudo apt-get install --yes \
  git cmake make \
  gcc-arm-none-eabi libstdc++-arm-none-eabi-newlib \
  openocd can-utils
```

Проверьте, что инструменты доступны:

```bash
cmake --version
arm-none-eabi-g++ --version
openocd --version
```

### Альтернатива: VS Code Dev Container

Если на компьютере установлены Docker, VS Code и расширение Dev Containers, откройте репозиторий в VS Code и выполните команду `Dev Containers: Reopen in Container`. Конфигурация из `.devcontainer` установит компилятор ARM, CMake, OpenOCD, clang-tidy и `can-utils`.

Для прошивки через ST-Link контейнер запускается с доступом к USB. На Linux Docker также должен иметь доступ к `/dev/bus/usb`; на Windows и macOS проброс USB настраивается средствами Docker Desktop или WSL отдельно.

## Получение проекта

Рекомендуемый способ — клонировать сразу с submodule:

```bash
git clone --recurse-submodules https://github.com/klegot/Pressure_Sensor.git
cd Pressure_Sensor
```

Если проект уже был клонирован без submodule:

```bash
git submodule update --init --recursive
```

Проверка версии CANoopEn:

```bash
git -C Drivers/CANoopEn describe --tags --exact-match
git -C Drivers/CANoopEn rev-parse HEAD
```

Ожидаемый результат:

```text
3.1.10
b421de2b80c69becb24f17e02512ba5e9188bae7
```

## Сборка

### Debug

```bash
cmake --preset Debug
cmake --build --preset Debug --parallel
```

Результат:

```text
build/Debug/Pressure_Sensor_SAUVC.elf
```

### Release

```bash
cmake --preset Release
cmake --build --preset Release --parallel
```

Результат:

```text
build/Release/Pressure_Sensor_SAUVC.elf
```

Linker script ограничивает прошивку реальными ресурсами STM32F103C8: 64 КБ Flash и 20 КБ RAM. При переполнении сборка завершится ошибкой.

Для получения бинарного файла из ELF:

```bash
arm-none-eabi-objcopy -O binary \
  build/Release/Pressure_Sensor_SAUVC.elf \
  build/Release/Pressure_Sensor_SAUVC.bin
```

### Статический анализ

После Debug-конфигурации можно запустить clang-tidy:

```bash
cmake --build build/Debug --target clang-tidy
```

Для этой команды должен быть установлен `clang-tidy`.

## Подключение аппаратуры

### Важное предупреждение

PA11 и PA12 — логические выводы bxCAN. **Их нельзя подключать непосредственно к CAN_H и CAN_L.** Между STM32 и шиной обязателен внешний CAN-трансивер, рассчитанный на логические уровни используемой платы.

### Используемые выводы STM32F103

| Вывод | Назначение |
|---|---|
| `PA11` | CAN1 RX |
| `PA12` | CAN1 TX |
| `PA5` | SPI1 SCK для ADS1220 |
| `PA6` | SPI1 MISO |
| `PA7` | SPI1 MOSI |
| `PA4` | ADS1220 chip select |
| `PA3` | ADS1220 DRDY, active low |
| `PA0` | `STM_ALIVE`, устанавливается в высокий уровень после успешной инициализации |
| `PA13` | SWDIO |
| `PA14` | SWCLK |

Прошивка ожидает внешний кварцевый генератор HSE `8 МГц`. PLL умножает его до `72 МГц`, а PCLK1 равен `36 МГц`.

CAN1 настроен так:

```text
36 МГц / (prescaler 9 × 16 TQ) = 250 кбит/с
16 TQ = 1 Sync + 13 BS1 + 2 BS2
sample point = 14 / 16 = 87,5 %
SJW = 1 TQ
```

Для физической CAN-шины:

- соедините CAN_H с CAN_H и CAN_L с CAN_L;
- соедините земли платы, трансивера и CAN-адаптера;
- установите терминаторы `120 Ом` на обоих концах магистрали;
- переведите трансивер из standby/silent режима;
- настройте все узлы на `250 кбит/с`;
- используйте активный CAN-интерфейс, который подтверждает кадры ACK-битом.

## Прошивка через ST-Link и OpenOCD

Подключите ST-Link к SWDIO, SWCLK, GND и опорному напряжению платы. Затем из корня проекта выполните:

```bash
openocd \
  -f interface/stlink.cfg \
  -f target/stm32f1x.cfg \
  -c "program build/Debug/Pressure_Sensor_SAUVC.elf verify reset exit"
```

Для Release замените `build/Debug` на `build/Release`.

После reset отдельная команда запуска не нужна: это bare-metal прошивка, основной цикл начинается автоматически.

### Запуск отладчика в VS Code

1. Установите расширения CMake Tools, clangd и Cortex-Debug.
2. Выполните Debug-сборку.
3. Подключите ST-Link.
4. Откройте панель **Run and Debug**.
5. Выберите конфигурацию **Cortex Debug f1** из `.vscode/launch.json`.

Конфигурация использует OpenOCD, `interface/stlink.cfg`, `target/stm32f1x.cfg` и останавливается в `main()`.

## Проверка CANopen через SocketCAN

Пример ниже использует Linux, интерфейс `can0` и пакет `can-utils`. Название интерфейса вашего адаптера может отличаться.

Настройка CAN:

```bash
sudo ip link set can0 down 2>/dev/null || true
sudo ip link set can0 type can bitrate 250000 restart-ms 100
sudo ip link set can0 up
```

В первом терминале запустите просмотр кадров:

```bash
candump -tz can0
```

После reset платы ожидается:

```text
can0  703  [1]  00       # boot-up
can0  703  [1]  7D       # heartbeat Pre-operational, раз в секунду
```

До NMT Start кадров `0x183` быть не должно.

### Перевод в Operational

```bash
cansend can0 000#0103
```

После команды heartbeat содержит `05`, а TPDO `0x183` появляется примерно 20 раз в секунду:

```text
can0  703  [1]  05
can0  183  [8]  .. .. .. .. .. .. .. ..
```

Остановить узел:

```bash
cansend can0 000#0203
```

Вернуть в Pre-operational:

```bash
cansend can0 000#8003
```

### Примеры SDO-чтения

SDO upload request состоит из команды `0x40`, младшего и старшего байтов index, sub-index и четырёх нулевых байтов.

```bash
# 0x1000:00 — device type
cansend can0 603#4000100000000000

# 0x1018:01 — vendor ID
cansend can0 603#4018100100000000

# 0x9130:01 — давление, Па
cansend can0 603#4030910100000000

# 0x2000:00 — глубина, мм
cansend can0 603#4000200000000000

# 0x6131:01 — единица давления
cansend can0 603#4031610100000000

# 0x6150:01 — статус канала
cansend can0 603#4050610100000000
```

Ответ приходит с CAN-ID `0x583`. Значение в expedited response также передаётся little-endian.

Проверка защиты read-only объекта:

```bash
# Попытка записать 0 в 0x9130:01
cansend can0 603#2330910100000000
```

Ожидается SDO abort: первый байт ответа `0x80`, abort code `0x06010002` — попытка записи read-only объекта.

## Диагностика проблем

### Ошибка сборки: не найден заголовок CANoopEn

Submodule не загружен:

```bash
git submodule update --init --recursive
```

### Нет ни boot-up, ни heartbeat

Проверьте:

- питание и reset STM32;
- наличие HSE 8 МГц;
- CAN-трансивер и его standby-вывод;
- PA11/PA12;
- общую землю;
- скорость адаптера 250 кбит/с;
- CAN_H/CAN_L и терминаторы.

### Boot-up постоянно повторяется или контроллер уходит в bus-off

Частые причины:

- на шине нет второго активного узла, который формирует ACK;
- CAN-анализатор работает только в silent/listen-only режиме;
- перепутаны CAN_H и CAN_L;
- разные скорости CAN;
- отсутствует или неверна терминация.

Auto-retransmission и automatic bus-off recovery включены. После устранения физической ошибки bxCAN автоматически пытается вернуться в работу.

### Heartbeat есть, TPDO нет

Это нормальное поведение в Pre-operational. Отправьте NMT Start:

```bash
cansend can0 000#0103
```

Если heartbeat уже `05`, проверьте ADS1220, DRDY и status `0x6150:01` через SDO.

### Глубина после включения равна нулю

Это ожидаемо: первая корректная выборка принимается за атмосферное давление. Следующие значения глубины рассчитываются относительно неё.

### CANopen-конфигуратор показывает неправильные COB-ID

При импорте `PressureSensor.eds` задайте Node-ID `3`. Выражения `$NODEID+0x180`, `$NODEID+0x580` и `$NODEID+0x600` тогда превращаются в `0x183`, `0x583` и `0x603`.

## Ограничения текущей версии

- фиксированные Node-ID 3 и 250 кбит/с;
- только один измерительный канал давления;
- только один TPDO, RPDO отсутствуют;
- нет SYNC, TIME, EMCY, LSS и сохранения параметров в энергонезависимой памяти;
- единицы, scaling, offset, фильтры, пределы, tare и auto-zero не изменяются по CANopen;
- статус ошибки доступен через SDO, но EMCY не отправляется;
- официальное CANopen conformance-тестирование не выполнялось.
