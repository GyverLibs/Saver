[![latest](https://img.shields.io/github/v/release/GyverLibs/SAVER.svg?color=brightgreen)](https://github.com/GyverLibs/SAVER/releases/latest/download/SAVER.zip)
[![PIO](https://badges.registry.platformio.org/packages/gyverlibs/library/SAVER.svg)](https://registry.platformio.org/libraries/gyverlibs/SAVER)
[![Foo](https://img.shields.io/badge/Website-AlexGyver.ru-blue.svg?style=flat-square)](https://alexgyver.ru/)
[![Foo](https://img.shields.io/badge/%E2%82%BD%24%E2%82%AC%20%D0%9F%D0%BE%D0%B4%D0%B4%D0%B5%D1%80%D0%B6%D0%B0%D1%82%D1%8C-%D0%B0%D0%B2%D1%82%D0%BE%D1%80%D0%B0-orange.svg?style=flat-square)](https://alexgyver.ru/support_alex/)
[![Foo](https://img.shields.io/badge/README-ENGLISH-blueviolet.svg?style=flat-square)](https://github-com.translate.goog/GyverLibs/SAVER?_x_tr_sl=ru&_x_tr_tl=en)  

[![Foo](https://img.shields.io/badge/ПОДПИСАТЬСЯ-НА%20ОБНОВЛЕНИЯ-brightgreen.svg?style=social&logo=telegram&color=blue)](https://t.me/GyverLibs)

# Saver
Библиотека для хранения бинарных настроек и других статических данных в EEPROM или файлах

- Контроль целостности данных
- Автоматическое обнаружение изменений по CRC
- Отложенная запись после прекращения изменений
- Версия данных для сброса несовместимого формата
- Сохранение старых полей при увеличении структуры
- Три стратегии записи файлов: Direct / Atomic / Backup
- Восстановление после незавершённой записи
- Отдельный FileStore для безопасной записи любых файлов
- Это более удобная и безопасная замена библиотекам [EEManager](https://github.com/GyverLibs/EEManager) и [FileData](https://github.com/GyverLibs/FileData)

### Совместимость
- Arduino-платформы со стандартной `EEPROM.h`
- ESP8266 / ESP32 с файловыми системами на базе `fs::FS`

## API
### SaverEE

```cpp
SaverEE(T& data, uint16_t addr = 0, uint8_t ver = 'A', uint8_t toutSec = 10);
SaverEE(void* data, uint16_t size, uint16_t addr = 0, uint8_t ver = 'A', uint8_t toutSec = 10);
```

- `data` - переменная, структура, массив или другой блок данных
- `size` - размер блока для raw-конструктора
- `addr` - адрес начала блока в EEPROM
- `ver` - версия формата данных, удобно задавать символом `'A'`, `'B'`...
- `toutSec` - максимальный ориентировочный таймаут автосохранения, секунды
- `toutSec = 0` - автосохранение выключено
- по умолчанию `toutSec = 10`

EE позволяет записывать блоки подряд:

```cpp
SaverEE cfgSaver(cfg, 0, 'A');
SaverEE statSaver(stat, cfgSaver.nextAddr(), 'A');
```

### SaverFile

```cpp
SaverFile(fs::FS& fs, const char* path, T& data, uint8_t ver = 'A', uint8_t toutSec = 10, FileStore::Mode mode = FileStore::Atomic);
SaverFile(fs::FS& fs, const char* path, void* data, uint16_t size, uint8_t ver = 'A', uint8_t toutSec = 10, FileStore::Mode mode = FileStore::Atomic);
```

- `fs` - файловая система
- `path` - путь к файлу, например `"/config.cfg"`
- Остальные аргументы аналогичны `SaverEE`
- `mode` - стратегия записи файла: `FileStore::Direct`, `FileStore::Atomic` или `FileStore::Backup`
- По умолчанию `SaverFile` использует `FileStore::Atomic`

Для режимов `Atomic` и `Backup` FileStore добавляет к пути суффиксы `.t` и `.b`. Внутренний буфер пути имеет размер 32 байта, поэтому длина исходного пути должна быть не больше 29 символов. Для `Direct` это ограничение не требуется.

### Методы
```cpp
// запустить систему, прочитать данные. allowGrow - разрешить увеличение без сброса к заводским настройкам
Status begin(bool allowGrow = false);

// запустить систему, прочитать данные с разрешением увеличения без сброса к заводским настройкам
Status beginGrow();

// записать данные в память. force - записывать в любом случае, даже если они не менялись
Status write(bool force = false);

// инвалидировать блок (данные сбросятся на умолчания при следующем запуске программы и вызове begin)
Status reset();

// сбросить до указанных значений прямо сейчас
Status reset(const T& data);
Status reset(const void* data, uint16_t size);

// тикер автоматического режима, allowWrite - разрешить физическую запись
Status tick(bool allowWrite = true);

// размер данных
uint16_t dataSize();

// версия
uint8_t version();

// установить максимальный таймаут автосохранения, секунды, макс. 120. 0 чтобы отключить авто-режим
void setTimeout(uint8_t timeout);
```

Дополнительно у `SaverEE`:

```cpp
// размер всего блока (данные + хэдер)
uint16_t blockSize();

// стартовый адрес блока
uint16_t startAddr();

// адрес для следующего блока
uint16_t nextAddr();
```

### Статусы

```cpp
Saver::None       // 0, ничего не произошло
Saver::Read       // данные успешно прочитаны
Saver::Write      // данные записаны
Saver::Default    // storage был пустой/невалидный/несовместимый, записаны текущие значения RAM
Saver::Grow       // прочитана старая часть увеличенной структуры и сохранён новый блок
Saver::Error      // ошибка EEPROM / FS / пути / размера
```

## Использование
### Как это работает
В программе глобально создаётся некая структура или другой формат данных, в котором хранятся настройки-параметры, т.е. набор переменных, которые используются в самой программе - читаются при использовании и изменяются юзером при помощи кнопок/крутилок/вебморды. В самой структуре указываются значения по умолчанию:

```cpp
struct Config {
    int value = 123;
    bool enabled = true;
};

Config config;
```

Данная библиотека позволяет "подключить" указанный блок настроек и автоматически синхронизировать его с EEPROM или файлом во флеш памяти:

```cpp
// по адресу 0 в EEPROM
SaverEE saver(config, 0);

// в файле "/config.cfg" на LittleFS
SaverFile saver(LittleFS, "/config.cfg", config);
```

Вызов `begin()` читает данные из выбранного хранилища и копирует в подключенный экземпляр. Если данные повреждены, имеют другую версию или несовместимый размер - текущие данные из RAM считаются значениями по умолчанию и записываются в хранилище, возвращается `Default`.

Вызов `begin(true)` или `beginGrow()` разрешает сохранить старые поля при **увеличении размера** структуры - это очень удобно при разработке, когда постепенно добавляются новые параметры, а старые сбрасывать до умолчаний не хочется.

Ручной вызов `write()` записывает данные в хранилище. Но в библиотеке предусмотрен автоматический режим: Saver проверяет CRC с периодом `timeout / 2`. Если CRC изменился - новое значение запоминается. Если на следующей проверке CRC не изменился и отличается от сохранённого - данные записываются. Таким образом при `timeout = 5` запись произойдёт примерно через 2.5-5 секунд после последнего изменения. Для этого режима нужно вызывать `tick()` в `loop()`:

```cpp
void loop() {
    saver.tick();
}
```

По умолчанию `timeout = 10`, поэтому CRC проверяется примерно раз в 5 секунд. На AVR расчёт CRC16 занимает около 4 микросекунд на байт, поэтому даже для 100 байт конфига это равносильно ~3 вызовам analogRead.

Параметр `ver` - версия бинарного формата:

```cpp
SaverEE saver(config, 0, 'A');
```

При несовместимом изменении структуры можно изменить версию:

```cpp
SaverEE saver(config, 0, 'B');
```

Если версия в хранилище не совпадает, **при запуске** Saver оставит текущие значения RAM и запишет их как новые значения по умолчанию - удобно использовать для "сброса на заводские". При помощи `reset()` можно сбросить до умолчаний прямо из программы. Важный момент - такой сброс сработает только при перезагрузке программы, когда сам конфиг естественным образом создастся с умолчаниями.

Второй вариант сброса - передать в `reset()` объект с умолчаниями, например новую структуру: `saver.reset(Config())`. В этом случае сброс и обновление хранилища произойдут сразу же.

Некоторые процессы могут привести к крашу во время записи, поэтому в `tick()` можно передавать разрешение записи:

```cpp
saver.tick(!radioBusy);
```

По умолчанию запись разрешена.

### Безопасная запись

`SaverFile` использует отдельный helper `FileStore`, который поддерживает три стратегии записи:

```cpp
FileStore::Direct
FileStore::Atomic
FileStore::Backup
```

`Direct` записывает сразу в основной файл. Это самый простой и быстрый режим, но при сбое питания во время записи файл может быть повреждён.

`Atomic` сначала записывает новый файл во временный:

```text
/config.cfg.t
```

После успешной записи временный файл заменяет основной через `rename()`. Этот режим подходит для файловых систем, где replace через rename является атомарным, например LittleFS.

`Backup` использует трёхфайловую схему:

```text
/config.cfg
/config.cfg.t
/config.cfg.b
```

Новые данные сначала записываются в `.t`, старый основной файл переносится в `.b`, после чего `.t` становится основным. После успешного завершения backup удаляется. Этот режим не полагается на atomic replace и используется в `SaverFile` по умолчанию.

Перед чтением и записью `SaverFile` выполняет recovery: проверяет основной, временный и backup-файл по CRC и восстанавливает валидную копию после незавершённой записи.

Для LittleFS можно явно выбрать более лёгкий `Atomic`:

```cpp
SaverFile saver(
    LittleFS,
    "/config.cfg",
    config,
    'A',
    10,
    FileStore::Atomic
);
```

### FileStore

`FileStore` можно использовать отдельно от Saver для безопасной записи любых файлов:

```cpp
#include <FileStore.h>

FileStore store(FileStore::Atomic);

File file = store.open(LittleFS, "/data.bin", "w");
if (file) {
    bool ok = file.write(data, size) == size;
    store.close(LittleFS, "/data.bin", file, ok);
}
```

По умолчанию используется `Direct`:

```cpp
FileStore store;
```

Для удобства файловую систему можно привязать через factory:

```cpp
auto store = makeFileStore(LittleFS, FileStore::Atomic);

File file = store.open("/data.bin", "w");
if (file) {
    bool ok = file.write(data, size) == size;
    store.close("/data.bin", file, ok);
}
```

`FileStore` использует шаблонные методы и работает не только с `fs::FS`, но и с совместимыми обёртками, у которых есть методы `open`, `exists`, `remove` и `rename`.

Основные методы:

```cpp
FileStore(FileStore::Mode mode = FileStore::Direct);

File open(fs, path, mode = "r");
bool close(fs, path, file, bool ok = true);
bool recover(fs, path, validator);
bool remove(fs, path);
```

`recover()` принимает функцию-валидатор:

```cpp
store.recover(LittleFS, "/data.bin", [](File& file) {
    return file.size() > 0;
});
```

Для `Atomic` и `Backup` безопасная транзакционная запись поддерживается для режима `"w"`. Режимы `"a"`, `"r+"` и другие изменяющие режимы намеренно не поддерживаются, потому что требуют другой схемы транзакции.

`store.close()` завершает именно транзакционную запись: делает `flush`, закрывает файл и выполняет выбранную стратегию commit. Не нужно вызывать `file.close()` перед `store.close()`.

Одновременно можно вести транзакции по разным путям, но для одного логического пути должна быть только одна активная запись.

## Типовые сценарии
### Обычные настройки

```cpp
Config config;
SaverEE saver(config);

void setup() {
    saver.begin();
}

void loop() {
    saver.tick();
}
```

Меняй `config` в любом месте программы - Saver сам обнаружит изменение.

### Ручная запись

```cpp
config.value = 123;
saver.write();
```

### Без автосохранения

```cpp
SaverEE saver(config, 0, 'A', 0);

void setup() {
    saver.begin();
}

void saveConfig() {
    saver.write();
}
```

### Обработка результата запуска

```cpp
switch (saver.beginGrow()) {
    case Saver::Read:
        Serial.println("Data read");
        break;

    case Saver::Default:
        Serial.println("Defaults written");
        break;

    case Saver::Grow:
        Serial.println("Data grown");
        break;

    case Saver::Error:
        Serial.println("Storage error");
        break;

    default:
        break;
}
```

## Примеры

```cpp
#include <SaverEE.h>
#include <EEPROM.h>

struct Config {
    int value = 123;
    bool enabled = true;
};
Config config;

// по адресу 0 в EEPROM
SaverEE saver(config, 0);

void setup() {
    // EEPROM.begin();
    saver.begin();
}

void loop() {
    saver.tick();

    // Просто меняй config.value/enabled в программе
    // Saver сам увидит изменение и сохранит его после таймаута
}
```

```cpp
#include <SaverFile.h>
#include <LittleFS.h>

struct Config {
    int value = 123;
    bool enabled = true;
};
Config config;

// в файле "/config.cfg" на LittleFS
SaverFile saver(LittleFS, "/config.cfg", config);

void setup() {
    LittleFS.begin(true);
    saver.begin();
}

void loop() {
    saver.tick();

    // Просто меняй config.value/enabled в программе
    // Saver сам увидит изменение и сохранит его после таймаута
}
```

<a id="install"></a>

## Установка
- Библиотеку можно найти по названию **Saver** и установить через менеджер библиотек в:
    - Arduino IDE
    - Arduino IDE v2
    - PlatformIO
- [Скачать библиотеку](https://github.com/GyverLibs/Saver/archive/refs/heads/main.zip) .zip архивом для ручной установки:
    - Распаковать и положить в *C:\Program Files (x86)\Arduino\libraries* (Windows x64)
    - Распаковать и положить в *C:\Program Files\Arduino\libraries* (Windows x32)
    - Распаковать и положить в *Документы/Arduino/libraries/*
    - (Arduino IDE) автоматическая установка из .zip: *Скетч/Подключить библиотеку/Добавить .ZIP библиотеку…* и указать скачанный архив
- Читай более подробную инструкцию по установке библиотек [здесь](https://alexgyver.ru/arduino-first/#%D0%A3%D1%81%D1%82%D0%B0%D0%BD%D0%BE%D0%B2%D0%BA%D0%B0_%D0%B1%D0%B8%D0%B1%D0%BB%D0%B8%D0%BE%D1%82%D0%B5%D0%BA)
### Обновление
- Рекомендую всегда обновлять библиотеку: в новых версиях исправляются ошибки и баги, а также проводится оптимизация и добавляются новые фичи
- Через менеджер библиотек IDE: найти библиотеку как при установке и нажать "Обновить"
- Вручную: **удалить папку со старой версией**, а затем положить на её место новую. "Замену" делать нельзя: иногда в новых версиях удаляются файлы, которые останутся при замене и могут привести к ошибкам!

<a id="feedback"></a>

## Баги и обратная связь
При нахождении багов создавайте **Issue**, а лучше сразу пишите на почту [alex@alexgyver.ru](mailto:alex@alexgyver.ru)  
Библиотека открыта для доработки и ваших **Pull Request**'ов!

При сообщении о багах или некорректной работе библиотеки нужно обязательно указывать:
- Версия библиотеки
- Какой используется МК
- Версия SDK (для ESP)
- Версия Arduino IDE
- Корректно ли работают ли встроенные примеры, в которых используются функции и конструкции, приводящие к багу в вашем коде
- Какой код загружался, какая работа от него ожидалась и как он работает в реальности
- В идеале приложить минимальный код, в котором наблюдается баг. Не полотно из тысячи строк, а минимальный код
