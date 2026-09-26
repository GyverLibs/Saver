This is an automatic translation and may be incorrect in some places. See the source README and examples for authoritative information.

[![latest](https://img.shields.io/github/v/release/GyverLibs/SAVER.svg?color=brightgreen)](https://github.com/GyverLibs/SAVER/releases/latest/download/SAVER.zip)
[![PIO](https://badges.registry.platformio.org/packages/gyverlibs/library/SAVER.svg)](https://registry.platformio.org/libraries/gyverlibs/SAVER)
[![Foo](https://img.shields.io/badge/Website-AlexGyver.ru-blue.svg?style=flat-square)](https://alexgyver.ru/)
[![Foo](https://img.shields.io/badge/%E2%82%BD%24%E2%82%AC%20%D0%9F%D0%BE%D0%B4%D0%B4%D0%B5%D1%80%D0%B6%D0%B0%D1%82%D1%8C-%D0%B0%D0%B2%D1%82%D0%BE%D1%80%D0%B0-orange.svg?style=flat-square)](https://alexgyver.ru/support_alex/)
[![Foo](https://img.shields.io/badge/README-ENGLISH-blueviolet.svg?style=flat-square)](https://github-com.translate.goog/GyverLibs/SAVER?_x_tr_sl=ru&_x_tr_tl=en)  

[![Foo](https://img.shields.io/badge/ПОДПИСАТЬСЯ-НА%20ОБНОВЛЕНИЯ-brightgreen.svg?style=social&logo=telegram&color=blue)](https://t.me/GyverLibs)

# Saver
Library for storing binary settings and other static data in EEPROM or files

- Control of data integrity
- Automatic Change Detection by CRC
- Postponed recording after termination of changes
- Data version to reset incompatible format
- Preservation of old fields with increasing structure
- Secure file recording via temporary file + rename + recovery

### Compatibility
- Arduino-platforms with standard`EEPROM.h`
- ESP8266 / ESP32 with file-based systems`fs::FS`

## API
### SaverEE

```cpp
SaverEE(T& data, uint16_t addr = 0, uint8_t ver = 'A', uint8_t toutSec = 10);
SaverEE(void* data, uint16_t size, uint16_t addr = 0, uint8_t ver = 'A', uint8_t toutSec = 10);
```

- `data`variable, structure, array or other data block
- `size`Block size for raw designer
- `addr`- the address of the beginning of the block in EEPROM
- `ver`- version of the data format, it is convenient to set the symbol`'A'`, `'B'`...
- `toutSec`Maximum indicative auto-save timeout, seconds
- `toutSec = 0`- Autosave off.
- default`toutSec = 10`

EE allows you to record blocks in a row:

```cpp
SaverEE cfgSaver(cfg, 0, 'A');
SaverEE statSaver(stat, cfgSaver.nextAddr(), 'A');
```

### SaverFile

```cpp
SaverFile(fs::FS& fs, const char* path, T& data, uint8_t ver = 'A', uint8_t toutSec = 10);
SaverFile(fs::FS& fs, const char* path, void* data, uint16_t size, uint8_t ver = 'A', uint8_t toutSec = 10);
```

- `fs`- file system
- `path`- the path to the file, for example`"/config.cfg"`
- Other arguments are similar.`SaverEE`

For temporary and backup files, Saver adds suffixes to the path`.t`and`.b`. The internal path buffer has a size of 32 bytes, so the length of the original path should not be more than 29 characters.

### Methods
```cpp
// Start the system, read the data. allowGrow - Allow increase without reset to factory setting n
Status begin(bool allowGrow = false);

// start the system, read the data with an increase resolution without resetting to the factory setting n
Status beginGrow();

// write it down in memory. force - write down in any case, even if they have not changed
Status write(bool force = false);

// Disable the block (data will be dropped on silence at the next start of the program and call start)
Status reset();

// Reset to these values right now
Status reset(const T& data);
Status reset(const void* data, uint16_t size);

// Automatic ticker, call in the loop
Status tick();

// size
uint16_t dataSize();

// version
uint8_t version();

// set the maximum auto-save timeout, seconds, max. 120. 0 to turn off auto mode
void setTimeout(uint8_t timeout);
```

In addition,`SaverEE`:

```cpp
// entire block size (data + header)
uint16_t blockSize();

// blockhead
uint16_t startAddr();

// address
uint16_t nextAddr();
```

### Statuses

```cpp
Saver::None       // 0, nothing happened.
Saver::Read       // data successfully read
Saver::Write      // record
Saver::Default    // storage was empty / invalid / incompatible, the current RAM values are recorded
Saver::Grow       // The old part of the enlarged structure is read and the new block is saved.
Saver::Error      // EEPROM / FS error / path / size
```

## Use of use
### How it works.
In the program, a certain structure or other data format is globally created, in which the settings-parameters are stored, i.e. a set of variables that are used in the program itself - are read when used and changed by the user using buttons / twisters / webmolds. In the structure itself, the default values are indicated:

```cpp
struct Config {
    int value = 123;
    bool enabled = true;
};

Config config;
```

This library allows you to “connect” the specified settings block and automatically synchronize it with EEPROM or a file in a flash memory:

```cpp
// 0 at EEPROM
SaverEE saver(config, 0);

// in the file "/config.cfg" on LittleFS
SaverFile saver(LittleFS, "/config.cfg", config);
```

A challenge`begin()`reads data from the selected storage and copies it to the connected instance. If the data is corrupted, has a different version or an incompatible size, the current data from the RAM is considered to be the default values and is written to the repository.`Default`.

A challenge`begin(true)`or`beginGrow()`allows you to save the old fields while increasing the size of the structure - this is very convenient when developing, when new parameters are gradually added, and you do not want to reset the old ones until you are silent.

Manual call`write()`It records data in storage. But the library has an automatic mode: Saver checks the CRC with a period`timeout / 2`. If the CRC changes, the new value is remembered. If at the next check the CRC has not changed and differs from the saved one, the data is recorded. Thus, in`timeout = 5`The recording will take place approximately 2.5-5 seconds after the last change. For this regime, you need to call`tick()`into`loop()`:

```cpp
void loop() {
    saver.tick();
}
```

By default.`timeout = 10`The CRC is checked approximately once every 5 seconds. On AVR, CRC16 takes about 4 microseconds per byte, so even for 100 bytes of config, this is equivalent to ~3 analogRead calls.

Parameter`ver`Binary format version:

```cpp
SaverEE saver(config, 0, 'A');
```

If the structure is incompatible, you can change the version:

```cpp
SaverEE saver(config, 0, 'B');
```

If the version in the vault doesn’t match, ** upon launch** Saver will leave the current RAM values and write them down as the new default values – convenient to use for “factory reset”. With help.`reset()`You can drop it right out of the program. An important point is that such a reset will work only when the program is restarted, when the config itself is naturally created with silence.

The second option is to transfer to`reset()`An object with omission, such as a new structure:`saver.reset(Config())`. In this case, the reset and upgrade of the storage will occur immediately.

### Secure recording
The new file is first written to a temporary:

```text
/config.cfg.t
```

After successful recording, rename is performed. If the file system is unable to replace an existing file via rename, the old file is temporarily transferred to:

```text
/config.cfg.b
```

The following`begin()`Saver checks the main, temporary and backup file and restores a valid copy after an unfinished record. CRC protects data from downloading a damaged or unwritten block.

## Model scenarios
### Normal settings

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

Change.`config`Anywhere in the program, Saver will detect the change.

### Manual recording

```cpp
config.value = 123;
saver.write();
```

### No auto-save.

```cpp
SaverEE saver(config, 0, 'A', 0);

void setup() {
    saver.begin();
}

void saveConfig() {
    saver.write();
}
```

### Processing the launch result

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

## Examples

```cpp
#include <SaverEE.h>
#include <EEPROM.h>

struct Config {
    int value = 123;
    bool enabled = true;
};
Config config;

// 0 at EEPROM
SaverEE saver(config, 0);

void setup() {
    // EEPROM.begin();
    saver.begin();
}

void loop() {
    saver.tick();

    // Change config.value/enabled in the program
    // Saver will see the change and save it after a timeout
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

// in the file "/config.cfg" on LittleFS
SaverFile saver(LittleFS, "/config.cfg", config);

void setup() {
    LittleFS.begin();
    saver.begin();
}

void loop() {
    saver.tick();

    // Change config.value/enabled in the program
    // Saver will see the change and save it after a timeout
}
```

<a id="install"></a>

## Installation
- The library can be found by the name **Saver** and installed through the library manager in:
    - Arduino IDE
    - Arduino IDE v2
    - PlatformIO
- [Download the library](https://github.com/GyverLibs/Saver/archive/refs/heads/main.zip).zip archive for manual installation:
    - Unpack and put in *C:\Program Files (x86)\Arduino\libraries* (Windows x64)
    - Unpack and put in *C:\Program Files\Arduino\libraries* (Windows x32)
    - Unpack and put in *Documents/Arduino/libraries/ *
    - (Arduino IDE) Automatic installation from .zip: *Sketch/Connect library/Add .ZIP library...* and specify downloaded archive
- Read more detailed instructions for installing libraries[here](https://alexgyver.ru/arduino-first/#%D0%A3%D1%81%D1%82%D0%B0%D0%BD%D0%BE%D0%B2%D0%BA%D0%B0_%D0%B1%D0%B8%D0%B1%D0%BB%D0%B8%D0%BE%D1%82%D0%B5%D0%BA)
### Update
- I recommend always updating the library: new versions fix errors and bugs, as well as optimize and add new features.
- Through the library manager IDE: find the library as when installing and click "Update"
- Manually: **Delete the folder with the old version** and then put the new one in its place. “Replacement” can not be done: sometimes new versions delete files that will remain when replaced and can lead to errors!

<a id="feedback"></a>

## Bugs and feedback
If you find bugs, create **Issue**, or better write to the mail immediately.[alex@alexgyver.ru](mailto:alex@alexgyver.ru)  
The library is open for revision and your **Pull Requests*!

When reporting bugs or incorrect work of the library, it is necessary to specify:
- Library version
- What is used by the IC
- SDK version (for ESP)
- Arduino IDE version
- Are embedded examples that use features and designs that cause bugs in your code working correctly?
- What code was downloaded, what work was expected from it and how it works in reality
- Ideally, attach the minimum code in which the bug is observed. Not a canvas of a thousand lines, but a minimum code.
