#pragma once
#include <Arduino.h>

class Saver {
   public:
    enum Status : uint8_t {
        None,     // 0, ничего не произошло
        Read,     // данные успешно прочитаны
        Write,    // данные записаны
        Default,  // storage был пустой/невалидный/несовместимый, записаны текущие значения RAM
        Grow,     // прочитана старая часть увеличенной структуры и сохранён новый блок
        Error,    // ошибка EEPROM / FS / пути / размера
    };

    // размер данных
    uint16_t dataSize() const {
        return _size;
    }

    // версия
    uint8_t version() const {
        return _ver;
    }

    // установить максимальный таймаут автосохранения, секунды, макс. 120. 0 чтобы отключить авто-режим
    void setTimeout(uint8_t timeout) {
        _timeout = timeout;
        _tmr = millis();
    }

   protected:
    struct __attribute__((packed)) Header {
        uint16_t crc;
        uint16_t size;
        uint8_t ver;

        bool match(uint16_t nsize, uint8_t nver) const {
            return size == nsize && ver == nver;
        }

        bool canGrow(uint16_t nsize, uint8_t nver) const {
            return ver == nver && size && size < nsize;
        }
    };

    static_assert(sizeof(Header) == 5, "Invalid Saver header size");

    Saver(void* data, uint16_t size, uint8_t ver, uint8_t toutSec)
        : _data((uint8_t*)data), _size(size), _ver(ver) {
        setTimeout(toutSec);
    }

    uint16_t _calcCrc(uint16_t size, uint8_t ver, const uint8_t* data) const {
        uint16_t crc = 0xFFFF;
        _crcByte(crc, (uint8_t)size);
        _crcByte(crc, (uint8_t)(size >> 8));
        _crcByte(crc, ver);
        while (size--) _crcByte(crc, *data++);
        return crc;
    }

    uint16_t _calcCrc() const {
        return _calcCrc(_size, _ver, _data);
    }

    void _syncCrc(uint16_t crc) {
        _ramCrc = _storeCrc = crc;
        _synced = true;
    }

    bool _tickCore() {
        if (!_synced || !_timeout) return false;

        uint16_t ms = millis();
        if (uint16_t(ms - _tmr) < _timeout * 512u) return false;
        _tmr = ms;

        uint16_t crc = _calcCrc();
        if (crc != _ramCrc) {
            _ramCrc = crc;
            return false;
        }
        return crc != _storeCrc;
    }

    static void _crcByte(uint16_t& crc, uint8_t data) {
        crc ^= (uint16_t)data << 8;
        for (uint8_t i = 0; i < 8; i++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }

    uint8_t* _data;
    uint16_t _size;
    uint16_t _tmr = 0;
    uint16_t _ramCrc = 0;
    uint16_t _storeCrc = 0;
    uint8_t _timeout = 10;
    uint8_t _ver;
    bool _synced = false;
};
