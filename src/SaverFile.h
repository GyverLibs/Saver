#pragma once
#include <Arduino.h>
#include <FS.h>
#include <string.h>

#include "Saver.h"

class SaverFile : public Saver {
    static constexpr size_t PathSize = 32;

   public:
    template <typename T>
    SaverFile(fs::FS& fs, const char* path, T& data, uint8_t ver = 'A', uint8_t toutSec = 10)
        : Saver(&data, _typeSize<T>(), ver, toutSec), _fs(&fs), _path(path) {}

    SaverFile(fs::FS& fs, const char* path, void* data, uint16_t size, uint8_t ver = 'A', uint8_t toutSec = 10)
        : Saver(data, size, ver, toutSec), _fs(&fs), _path(path) {}

    // запустить систему, прочитать данные. allowGrow - разрешить увеличение без сброса к заводским настройкам
    Status begin(bool allowGrow = false) {
        _synced = false;
        if (!_valid()) return Error;
        if (!_recover()) return Error;

        if (!_fs->exists(_path)) {
            if (write(true) == Error) return Error;
            return Default;
        }

        File file = _fs->open(_path, "r");
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

        Path path(_path);

        _fs->remove(path.tmp());

        File file = _fs->open(path.tmp(), "w");
        if (!file) return Error;

        Header hdr = {_ramCrc, _size, _ver};
        bool ok = file.write((const uint8_t*)&hdr, sizeof(Header)) == sizeof(Header) &&
                  file.write((const uint8_t*)_data, _size) == _size;
        file.flush();
        file.close();

        if (!ok) {
            _fs->remove(path.tmp());
            return Error;
        }

        if (_fs->rename(path.tmp(), _path)) {
            _fs->remove(path.bak());
            _syncCrc(_ramCrc);
            return Write;
        }

        if (!_fs->exists(_path)) return Error;

        _fs->remove(path.bak());
        if (!_fs->rename(_path, path.bak())) return Error;

        if (!_fs->rename(path.tmp(), _path)) {
            _fs->rename(path.bak(), _path);
            return Error;
        }

        _fs->remove(path.bak());
        _syncCrc(_ramCrc);
        return Write;
    }

    // инвалидировать блок (данные сбросятся на умолчания при следующем запуске программы и вызове begin)
    Status reset() {
        if (!_valid()) return Error;

        Path path(_path);

        if (_fs->exists(path.tmp()) && !_fs->remove(path.tmp())) return Error;
        if (_fs->exists(path.bak()) && !_fs->remove(path.bak())) return Error;
        if (_fs->exists(_path) && !_fs->remove(_path)) return Error;

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

    // тикер автоматического режима, вызывать в loop
    Status tick() {
        return _tickCore() ? write() : None;
    }

   private:
    template <typename T>
    static constexpr uint16_t _typeSize() {
        static_assert(sizeof(T) <= UINT16_MAX, "Saver data is too large");
        return (uint16_t)sizeof(T);
    }

    bool _valid() const {
        return _fs && _path && _path[0] && _data && _size && strlen(_path) + 3 <= PathSize;
    }

    struct Path {
        char buf[PathSize];
        uint8_t len;

        Path(const char* path) : len(strlen(path)) {
            memcpy(buf, path, len);
        }

        const char* tmp() {
            return _suffix('t');
        }

        const char* bak() {
            return _suffix('b');
        }

       private:
        const char* _suffix(char c) {
            buf[len] = '.';
            buf[len + 1] = c;
            buf[len + 2] = 0;
            return buf;
        }
    };

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

    bool _validFile(const char* path) {
        File file = _fs->open(path, "r");
        if (!file || file.size() < sizeof(Header)) return false;

        Header hdr;
        bool ok = file.read((uint8_t*)&hdr, sizeof(Header)) == sizeof(Header) &&
                  file.size() == (size_t)sizeof(Header) + hdr.size;
        if (ok) ok = _storedCrc(file, hdr) == hdr.crc;
        file.close();
        return ok;
    }

    bool _recover() {
        Path path(_path);

        bool hasTmp = _fs->exists(path.tmp());
        bool hasBak = _fs->exists(path.bak());
        if (!hasTmp && !hasBak) return true;

        if (_fs->exists(_path) && _validFile(_path)) {
            if (hasTmp) _fs->remove(path.tmp());
            if (hasBak) _fs->remove(path.bak());
            return true;
        }

        if (hasTmp && _validFile(path.tmp())) {
            _fs->remove(_path);
            if (!_fs->rename(path.tmp(), _path)) return false;
            if (hasBak) _fs->remove(path.bak());
            return true;
        }

        if (hasBak && _validFile(path.bak())) {
            _fs->remove(_path);
            if (!_fs->rename(path.bak(), _path)) return false;
            if (hasTmp) _fs->remove(path.tmp());
            return true;
        }

        if (hasTmp) _fs->remove(path.tmp());
        if (hasBak) _fs->remove(path.bak());
        return true;
    }

    fs::FS* _fs;
    const char* _path;
};
