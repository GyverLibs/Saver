#include <Arduino.h>
#include <LittleFS.h>
#include <SaverFile.h>

struct Config {
    int value = 123;
    bool enabled = true;
};

Config config;

// fs, path, data, version, autosave seconds
SaverFile saver(LittleFS, "/config.cfg", config, 'A', 5);

void setup() {
    Serial.begin(115200);

    if (!LittleFS.begin()) {
        Serial.println("LittleFS error");
        return;
    }

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
    if (Serial.available()) {
        config.value = Serial.parseInt();
        while (Serial.available()) Serial.read();

        Serial.print("New value: ");
        Serial.println(config.value);
    }

    Saver::Status status = saver.tick();

    if (status == Saver::Write) {
        Serial.println("File saved");
    } else if (status == Saver::Error) {
        Serial.println("File error");
    }
}
