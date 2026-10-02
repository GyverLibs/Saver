#pragma once
#include <Arduino.h>
#include <FS.h>
#include <string.h>

#include "FileStore.h"
#include "Saver.h"

class SaverFile : public Saver {
   public:
    template <typename T>
    SaverFile(fs::FS& fs, const char* path, T& data, uint8_t ver = 'A', uint8_t toutSec = 10, FileStore::Mode mode = FileStore::Atomic)
        : Saver(&data, _typeSize<T>(), ver, toutSec), _fs(&fs), _path(path), _store(mode) {}

    SaverFile(fs::FS& fs, const char* path, void* data, uint16_t size, uint8_t ver = 'A', uint8_t toutSec = 10, FileStore::Mode mode = FileStore::Atomic)
        : Saver(data, size, ver, toutSec), _fs(&fs), _path(path), _store(mode) {}

    // запустить систему, прочитать данные. allowGrow - разрешить увеличение без сброса к заводским настройкам
    Status begin(bool allowGrow = false) {
        _synced = false;
        if (!_valid() || !_recover()) return Error;

        if (!_fs->exists(_path)) {
            if (write(true) == Error) return Error;
            return Default;
        }

        File file = _store.open(*_fs, _path);
        if (!file) return Error;

        Header hdr;
        bool headerOk = file.read((uint8_t*)&hdr, sizeof(Header)) == sizeof(Header);
        bool grow = headerOk && allowGrow && hdr.canGrow(_size, _ver);
        bool valid = headerOk && (hdr.match(_size, _ver) || grow) &&
                     file.size() == (size_t)sizeof(Header) + hdr.size &&
                     _storedCrc(file, hdr) == hdr.crc;

        if (valid) {
            if (!file.seek(sizeof(Header))) {
                file.close();
                return Error;
            }

            uint16_t readSize = grow ? hdr.size : _size;
            bool readOk = file.read(_data, readSize) == readSize;
            file.close();
            if (!readOk) return Error;

            if (grow) {
                if (write(true) == Error) return Error;
                return Grow;
            }

            _syncCrc(hdr.crc);
            return Read;
        }

        file.close();
        if (write(true) == Error) return Error;
        return Default;
    }

    // запустить систему, прочитать данные с разрешением увеличения без сброса к заводским настройкам
    Status beginGrow() {
        return begin(true);
    }

    // записать данные в память. force - записывать в любом случае, даже если они не менялись
    Status write(bool force = false) {
        if (!_valid()) return Error;

        _ramCrc = _calcCrc();
        if (!force && _synced && _ramCrc == _storeCrc) return None;
        if (!_recover()) return Error;

        File file = _store.open(*_fs, _path, "w");
        if (!file) return Error;

        Header hdr = {_ramCrc, _size, _ver};
        bool ok = file.write((const uint8_t*)&hdr, sizeof(Header)) == sizeof(Header) &&
                  file.write((const uint8_t*)_data, _size) == _size;

        if (!_store.close(*_fs, _path, file, ok)) return Error;

        _syncCrc(_ramCrc);
        return Write;
    }

    // инвалидировать блок (данные сбросятся на умолчания при следующем запуске программы и вызове begin)
    Status reset() {
        if (!_valid() || !_store.remove(*_fs, _path)) return Error;
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

   private:
    template <typename T>
    static constexpr uint16_t _typeSize() {
        static_assert(sizeof(T) <= UINT16_MAX, "Saver data is too large");
        return (uint16_t)sizeof(T);
    }

    bool _valid() const {
        return _fs && _data && _size && _store.valid(_path);
    }

    bool _recover() {
        return _store.recover(*_fs, _path, [this](File& file) { return _validFile(file); });
    }

    uint16_t _storedCrc(File& file, const Header& hdr) {
        uint16_t crc = 0xFFFF;
        _crcByte(crc, (uint8_t)hdr.size);
        _crcByte(crc, (uint8_t)(hdr.size >> 8));
        _crcByte(crc, hdr.ver);

        for (uint16_t i = 0; i < hdr.size; i++) {
            int b = file.read();
            if (b < 0) return (uint16_t)~hdr.crc;
            _crcByte(crc, (uint8_t)b);
        }
        return crc;
    }

    bool _validFile(File& file) {
        if (file.size() < sizeof(Header)) return false;

        Header hdr;
        bool ok = file.read((uint8_t*)&hdr, sizeof(Header)) == sizeof(Header) &&
                  file.size() == (size_t)sizeof(Header) + hdr.size;
        if (ok) ok = _storedCrc(file, hdr) == hdr.crc;
        return ok;
    }

    fs::FS* _fs;
    const char* _path;
    FileStore _store;
};
