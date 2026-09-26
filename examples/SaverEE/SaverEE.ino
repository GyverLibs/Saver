#include <Arduino.h>
#include <EEPROM.h>
#include <SaverEE.h>

struct Config {
    int value = 123;
    bool enabled = true;
};

Config config;

// data, EEPROM address, version, autosave seconds
SaverEE saver(config, 0, 'A', 5);

void setup() {
    Serial.begin(115200);

#if defined(ESP8266) || defined(ESP32)
    // Для одного блока достаточно его полного размера
    EEPROM.begin(saver.blockSize());
#endif

    // beginGrow() сохранит старые поля, если структура стала больше
    Saver::Status status = saver.beginGrow();

    Serial.print("Begin status: ");
    Serial.println(status);

    Serial.print("value = ");
    Serial.println(config.value);
    Serial.print("enabled = ");
    Serial.println(config.enabled);

    Serial.println("Send a number to change config.value");
}

void loop() {
    // Меняем настройки как обычную переменную
    if (Serial.available()) {
        config.value = Serial.parseInt();
        while (Serial.available()) Serial.read();

        Serial.print("New value: ");
        Serial.println(config.value);
    }

    // Saver сам заметит изменение и сохранит данные после таймаута
    Saver::Status status = saver.tick();

    if (status == Saver::Write) {
        Serial.println("EEPROM saved");
    } else if (status == Saver::Error) {
        Serial.println("EEPROM error");
    }
}
