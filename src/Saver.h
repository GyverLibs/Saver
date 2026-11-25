#pragma once
#include <Arduino.h>

struct SaverBlock {
    void* data;
    uint16_t size;
};

template <typename T>
SaverBlock saverBlock(T& data) {
    static_assert(sizeof(T) <= UINT16_MAX, "Saver block is too large");
    return {&data, (uint16_t)sizeof(T)};
}

struct SaverBlocks {
    const SaverBlock* data;
    uint16_t count;
};

template <size_t N>
SaverBlocks saverBlocks(const SaverBlock (&blocks)[N]) {
    static_assert(N <= UINT16_MAX, "Too many Saver blocks");
    return {blocks, (uint16_t)N};
}

inline SaverBlocks saverBlocks(const SaverBlock* blocks, uint16_t count) {
    return {blocks, count};
}

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

    Saver(const SaverBlock* blocks, uint16_t count, uint8_t ver, uint8_t toutSec)
        : Saver((void*)blocks, _blocksSize(blocks, count), ver, toutSec) {
        _blockCount = count;
    }

    bool _validData() const {
        if (!_data || !_size) return false;
        if (!_blockCount) return true;
        return _blocksSize((const SaverBlock*)_data, _blockCount) == _size;
    }

    template <typename F>
    bool _each(uint16_t size, F fn) const {
        if (!_blockCount) return fn(_data, size);
        const SaverBlock* blocks = (const SaverBlock*)_data;
        for (uint16_t i = 0; i < _blockCount && size; i++) {
            uint16_t n = blocks[i].size < size ? blocks[i].size : size;
            if (!fn((uint8_t*)blocks[i].data, n)) return false;
            size -= n;
        }
        return !size;
    }

    bool _copyData(const void* data, uint16_t size) const {
        const uint8_t* src = (const uint8_t*)data;
        return _each(size, [&](uint8_t* dst, uint16_t n) {
            memcpy(dst, src, n);
            src += n;
            return true;
        });
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
        uint16_t crc = 0xFFFF;
        _crcByte(crc, (uint8_t)_size);
        _crcByte(crc, (uint8_t)(_size >> 8));
        _crcByte(crc, _ver);
        _each(_size, [&](uint8_t* data, uint16_t size) {
            while (size--) _crcByte(crc, *data++);
            return true;
        });
        return crc;
    }

    static uint16_t _blocksSize(const SaverBlock* blocks, uint16_t count) {
        if (!blocks || !count) return 0;
        uint32_t size = 0;
        for (uint16_t i = 0; i < count; i++) {
            if (!blocks[i].data || !blocks[i].size) return 0;
            size += blocks[i].size;
            if (size > UINT16_MAX) return 0;
        }
        return (uint16_t)size;
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
    uint16_t _blockCount = 0;
    uint16_t _tmr = 0;
    uint16_t _ramCrc = 0;
    uint16_t _storeCrc = 0;
    uint8_t _timeout = 10;
    uint8_t _ver;
    bool _synced = false;
};
