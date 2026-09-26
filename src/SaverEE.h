#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>

#include "Saver.h"

class SaverEE : public Saver {
   public:
    template <typename T>
    SaverEE(T& data, uint16_t addr = 0, uint8_t ver = 'A', uint8_t toutSec = 10)
        : Saver(&data, _typeSize<T>(), ver, toutSec), _addr(addr) {}

    SaverEE(void* data, uint16_t size, uint16_t addr = 0, uint8_t ver = 'A', uint8_t toutSec = 10)
        : Saver(data, size, ver, toutSec), _addr(addr) {}

    // запустить систему, прочитать данные. allowGrow - разрешить увеличение без сброса к заводским настройкам
    Status begin(bool allowGrow = false) {
        _synced = false;
        if (!_validBlock()) return Error;

        Header hdr;
        _readHeader(hdr);

        bool grow = allowGrow && hdr.canGrow(_size, _ver);

        if ((hdr.match(_size, _ver) || grow) && _storedCrc(hdr) == hdr.crc) {
            uint16_t readSize = grow ? hdr.size : _size;
            for (uint16_t i = 0; i < readSize; i++) {
                _data[i] = EEPROM.read(_addr + sizeof(Header) + i);
            }

            if (grow) {
                if (write(true) == Error) return Error;
                return Grow;
            }

            _syncCrc(hdr.crc);
            return Read;
        }

        if (write(true) == Error) return Error;
        return Default;
    }

    // запустить систему, прочитать данные с разрешением увеличения без сброса к заводским настройкам
    Status beginGrow() {
        return begin(true);
    }

    // записать данные в память. force - записывать в любом случае, даже если они не менялись
    Status write(bool force = false) {
        if (!_validBlock()) return Error;

        _ramCrc = _calcCrc();
        if (!force && _synced && _ramCrc == _storeCrc) return None;

        Header hdr = {_ramCrc, _size, _ver};

        for (uint16_t i = 0; i < _size; i++) {
            _writeByte(_addr + sizeof(Header) + i, _data[i]);
        }

        const uint8_t* p = (const uint8_t*)&hdr;
        for (uint8_t i = 2; i < sizeof(Header); i++) _writeByte(_addr + i, p[i]);
        _writeByte(_addr, p[0]);
        _writeByte(_addr + 1, p[1]);

#if defined(ESP8266) || defined(ESP32)
        if (!EEPROM.commit()) return Error;
#endif

        _syncCrc(_ramCrc);
        return Write;
    }

    // инвалидировать блок (данные сбросятся на умолчания при следующем запуске программы и вызове begin)
    Status reset() {
        if (!_validBlock()) return Error;

        _writeByte(_addr, (uint8_t)~EEPROM.read(_addr));

#if defined(ESP8266) || defined(ESP32)
        if (!EEPROM.commit()) return Error;
#endif

        _synced = false;
        return Write;
    }

    // сбросить до указанных значений
    template <typename T>
    Status reset(const T& data) {
        static_assert(sizeof(T) <= UINT16_MAX, "Saver data is too large");
        return reset(&data, sizeof(T));
    }

    // сбросить до указанных значений
    Status reset(const void* data, uint16_t size) {
        if (!data || size != _size) return Error;

        memcpy(_data, data, size);
        return write(true);
    }

    // тикер автоматического режима, allowWrite - разрешить физическую запись
    Status tick(bool allowWrite = true) {
        if (!_tickCore()) return None;
        return allowWrite ? write() : None;
    }

    // размер всего блока (данные + хэдер)
    uint16_t blockSize() const {
        return sizeof(Header) + _size;
    }

    // стартовый адрес блока
    uint16_t startAddr() const {
        return _addr;
    }

    // адрес для следующего блока
    uint16_t nextAddr() const {
        return _addr + blockSize();
    }

   private:
    template <typename T>
    static constexpr uint16_t _typeSize() {
        static_assert(sizeof(T) <= UINT16_MAX, "Saver data is too large");
        return (uint16_t)sizeof(T);
    }

    bool _validBlock() const {
        if (!_data || !_size) return false;
        uint32_t end = (uint32_t)_addr + sizeof(Header) + _size;
        return end <= (uint32_t)EEPROM.length() && end <= UINT16_MAX;
    }

    void _readHeader(Header& hdr) const {
        uint8_t* p = (uint8_t*)&hdr;
        for (uint8_t i = 0; i < sizeof(Header); i++) p[i] = EEPROM.read(_addr + i);
    }

    uint16_t _storedCrc(const Header& hdr) const {
        uint16_t crc = 0xFFFF;
        _crcByte(crc, (uint8_t)hdr.size);
        _crcByte(crc, (uint8_t)(hdr.size >> 8));
        _crcByte(crc, hdr.ver);
        for (uint16_t i = 0; i < hdr.size; i++) {
            _crcByte(crc, EEPROM.read(_addr + sizeof(Header) + i));
        }
        return crc;
    }

    void _writeByte(uint16_t addr, uint8_t value) {
#if defined(ESP8266) || defined(ESP32)
        EEPROM.write(addr, value);
#else
        EEPROM.update(addr, value);
#endif
    }

    uint16_t _addr;
};
