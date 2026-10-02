#pragma once
#include <Arduino.h>
#include <FS.h>
#include <string.h>

class FileStore {
   public:
    enum Mode : uint8_t {
        Direct,
        Atomic,
        Backup
    };

    FileStore(Mode mode = Direct) : _mode(mode) {}

    bool valid(const char* path) const {
        return path && path[0] && (_mode == Direct || strlen(path) + 3 <= PathSize);
    }

    template <typename FS>
    File open(FS& fs, const char* path, const char* mode = "r") {
        if (!valid(path) || !mode) return File();
        if (_mode == Direct || (mode[0] == 'r' && !strchr(mode, '+'))) return fs.open(path, mode);
        if (mode[0] != 'w') return File();

        Path p(path);
        if (!_remove(fs, p.tmp())) return File();

        return fs.open(p.tmp(), mode);
    }

    template <typename FS, typename Validator>
    File open(FS& fs, const char* path, const char* mode, Validator validator) {
        return recover(fs, path, validator) ? open(fs, path, mode) : File();
    }

    template <typename FS>
    bool close(FS& fs, const char* path, File& file, bool ok = true) {
        if (!file) return false;
        file.flush();
        file.close();

        if (_mode == Direct) return ok;
        if (!valid(path)) return false;

        Path p(path);
        if (!ok) {
            _remove(fs, p.tmp());
            return false;
        }

        if (_mode == Atomic || !fs.exists(path)) return fs.rename(p.tmp(), path);
        if (!_remove(fs, p.bak()) || !fs.rename(path, p.bak())) return false;

        if (fs.rename(p.tmp(), path)) {
            _remove(fs, p.bak());
            return true;
        }

        fs.rename(p.bak(), path);
        return false;
    }

    template <typename FS, typename Validator>
    bool recover(FS& fs, const char* path, Validator validator) {
        if (_mode == Direct) return valid(path);
        if (!valid(path)) return false;

        Path p(path);
        bool tmp = fs.exists(p.tmp());
        bool bak = _mode == Backup && fs.exists(p.bak());
        if (!tmp && !bak) return true;

        if (fs.exists(path) && _validFile(fs, path, validator)) {
            if (tmp) fs.remove(p.tmp());
            if (bak) fs.remove(p.bak());
            return true;
        }
        if (tmp && _validFile(fs, p.tmp(), validator)) {
            if (!_restore(fs, p.tmp(), path)) return false;
            if (bak) fs.remove(p.bak());
            return true;
        }
        if (bak && _validFile(fs, p.bak(), validator)) {
            if (!_restore(fs, p.bak(), path)) return false;
            if (tmp) fs.remove(p.tmp());
            return true;
        }
        if (tmp) fs.remove(p.tmp());
        if (bak) fs.remove(p.bak());
        return true;
    }

    template <typename FS>
    bool remove(FS& fs, const char* path) {
        if (!valid(path)) return false;

        if (_mode != Direct) {
            Path p(path);
            if (!_remove(fs, p.tmp())) return false;
            if (_mode == Backup && !_remove(fs, p.bak())) return false;
        }

        return _remove(fs, path);
    }

   private:
    static constexpr size_t PathSize = 32;

    struct Path {
        char buf[PathSize];
        uint8_t len;

        Path(const char* path) : len(strlen(path)) { memcpy(buf, path, len); }
        const char* tmp() { return suffix('t'); }
        const char* bak() { return suffix('b'); }

       private:
        const char* suffix(char c) {
            buf[len] = '.';
            buf[len + 1] = c;
            buf[len + 2] = 0;
            return buf;
        }
    };

    template <typename FS>
    static bool _remove(FS& fs, const char* path) {
        return !fs.exists(path) || fs.remove(path);
    }

    template <typename FS>
    static bool _restore(FS& fs, const char* from, const char* to) {
        return _remove(fs, to) && fs.rename(from, to);
    }

    template <typename FS, typename Validator>
    static bool _validFile(FS& fs, const char* path, Validator validator) {
        File file = fs.open(path, "r");
        if (!file) return false;

        bool ok = validator(file);
        file.close();
        return ok;
    }

    Mode _mode;
};

template <typename FS>
class FileStoreFS : public FileStore {
   public:
    FileStoreFS(FS& fs, Mode mode = Direct) : FileStore(mode), _fs(fs) {}

    File open(const char* path, const char* mode = "r") {
        return FileStore::open(_fs, path, mode);
    }

    template <typename Validator>
    File open(const char* path, const char* mode, Validator validator) {
        return FileStore::open(_fs, path, mode, validator);
    }

    bool close(const char* path, File& file, bool ok = true) {
        return FileStore::close(_fs, path, file, ok);
    }

    template <typename Validator>
    bool recover(const char* path, Validator validator) {
        return FileStore::recover(_fs, path, validator);
    }

    bool remove(const char* path) { return FileStore::remove(_fs, path); }

   private:
    FS& _fs;
};

template <typename FS>
FileStoreFS<FS> makeFileStore(FS& fs, FileStore::Mode mode = FileStore::Direct) {
    return FileStoreFS<FS>(fs, mode);
}
