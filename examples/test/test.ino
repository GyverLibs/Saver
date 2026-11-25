#include <Arduino.h>
#include <FileStore.h>
#include <LittleFS.h>
#include <SaverFile.h>

// Полный тест FileStore + SaverFile
// Для своих файлов использует префиксы /fst_* и /sv_*

static uint16_t g_pass = 0;
static uint16_t g_fail = 0;

void check(bool ok, const char* name) {
    Serial.print(ok ? "[PASS] " : "[FAIL] ");
    Serial.println(name);
    if (ok) ++g_pass;
    else ++g_fail;
}

void sidePath(char* out, const char* path, char suffix) {
    snprintf(out, 32, "%s.%c", path, suffix);
}

void cleanup(const char* path) {
    char p[32];
    if (LittleFS.exists(path)) LittleFS.remove(path);
    sidePath(p, path, 't');
    if (LittleFS.exists(p)) LittleFS.remove(p);
    sidePath(p, path, 'b');
    if (LittleFS.exists(p)) LittleFS.remove(p);
}

bool writeText(const char* path, const char* text) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    size_t len = strlen(text);
    bool ok = f.write((const uint8_t*)text, len) == len;
    f.flush();
    f.close();
    return ok;
}

bool fileEquals(const char* path, const char* text) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;

    size_t len = strlen(text);
    if (f.size() != len) {
        f.close();
        return false;
    }

    for (size_t i = 0; i < len; ++i) {
        if (f.read() != (uint8_t)text[i]) {
            f.close();
            return false;
        }
    }
    f.close();
    return true;
}

// Test records beginning with "OK:" are considered valid.
bool validText(File& file) {
    if (file.size() < 3 || !file.seek(0)) return false;
    return file.read() == 'O' &&
           file.read() == 'K' &&
           file.read() == ':';
}

void makeState(const char* path, uint8_t state, const char* valid, const char* invalid) {
    if (LittleFS.exists(path)) LittleFS.remove(path);
    if (state == 1) writeText(path, valid);
    else if (state == 2) writeText(path, invalid);
}

// state: 0 = missing, 1 = valid, 2 = invalid
bool expectedMain(const char* path, uint8_t state, const char* valid, const char* invalid) {
    if (!state) return !LittleFS.exists(path);
    return fileEquals(path, state == 1 ? valid : invalid);
}

void testDirect() {
    Serial.println("\n--- FileStore Direct ---");

    const char* path = "/fst_dir";
    cleanup(path);

    FileStore store;

    File f = store.open(LittleFS, path, "w");
    bool opened = (bool)f;
    bool wrote = opened && f.write((const uint8_t*)"ONE", 3) == 3;
    bool closed = opened && store.close(LittleFS, path, f, wrote);

    check(opened && wrote && closed && fileEquals(path, "ONE"),
          "Direct write");

    f = store.open(LittleFS, path, "a");
    opened = (bool)f;
    wrote = opened && f.write((const uint8_t*)"X", 1) == 1;
    closed = opened && store.close(LittleFS, path, f, wrote);

    check(opened && wrote && closed && fileEquals(path, "ONEX"),
          "Direct passes append mode through");

    cleanup(path);
}

void testAtomic() {
    Serial.println("\n--- FileStore Atomic ---");

    const char* path = "/fst_at";
    char tmp[32];
    sidePath(tmp, path, 't');

    FileStore store(FileStore::Atomic);

    // Replace existing file
    cleanup(path);
    writeText(path, "OLD");

    File f = store.open(LittleFS, path, "w");
    bool opened = (bool)f;
    bool wrote = opened && f.write((const uint8_t*)"NEW", 3) == 3;

    check(opened && wrote &&
              fileEquals(path, "OLD") &&
              LittleFS.exists(tmp),
          "Atomic stages write in .t");

    bool closed = opened && store.close(LittleFS, path, f, wrote);
    check(closed &&
              fileEquals(path, "NEW") &&
              !LittleFS.exists(tmp),
          "Atomic commit replaces main");

    // Failed writer must preserve old main and remove temp
    cleanup(path);
    writeText(path, "OLD");
    f = store.open(LittleFS, path, "w");
    opened = (bool)f;
    if (opened) f.write((const uint8_t*)"BROKEN", 6);
    closed = opened && store.close(LittleFS, path, f, false);

    check(!closed &&
              fileEquals(path, "OLD") &&
              !LittleFS.exists(tmp),
          "Atomic close(false) preserves main");

    // Unsupported mutating modes
    File fa = store.open(LittleFS, path, "a");
    File frp = store.open(LittleFS, path, "r+");
    check(!fa && !frp && fileEquals(path, "OLD"),
          "Atomic rejects a and r+");

    // Manual close before store.close()
    cleanup(path);
    writeText(path, "OK:OLD");
    f = store.open(LittleFS, path, "w");
    opened = (bool)f;
    if (opened) {
        f.write((const uint8_t*)"OK:NEW", 6);
        f.flush();
        f.close();
    }
    bool commitAfterManualClose = store.close(LittleFS, path, f);
    bool recovered = store.recover(LittleFS, path, validText);

    check(opened &&
              !commitAfterManualClose &&
              recovered &&
              fileEquals(path, "OK:OLD") &&
              !LittleFS.exists(tmp),
          "Manual file.close() does not commit; recovery keeps main");

    cleanup(path);
}

void testAtomicRecoveryMatrix() {
    Serial.println("\n--- Atomic recovery matrix (9 states) ---");

    const char* path = "/fst_am";
    char tmp[32];
    sidePath(tmp, path, 't');

    FileStore store(FileStore::Atomic);

    for (uint8_t mainState = 0; mainState < 3; ++mainState) {
        for (uint8_t tmpState = 0; tmpState < 3; ++tmpState) {
            cleanup(path);

            makeState(path, mainState, "OK:M", "BAD:M");
            makeState(tmp, tmpState, "OK:T", "BAD:T");

            bool ok = store.recover(LittleFS, path, validText);

            uint8_t expectedState = mainState;
            const char* expectedValid = "OK:M";
            const char* expectedInvalid = "BAD:M";

            if (tmpState != 0) {
                if (mainState == 1) {
                    expectedState = 1;
                    expectedValid = "OK:M";
                } else if (tmpState == 1) {
                    expectedState = 1;
                    expectedValid = "OK:T";
                }
            }

            bool result = ok &&
                          expectedMain(path, expectedState, expectedValid, expectedInvalid) &&
                          !LittleFS.exists(tmp);

            char name[64];
            snprintf(name, sizeof(name),
                     "Atomic recovery main=%u tmp=%u",
                     mainState, tmpState);
            check(result, name);
        }
    }

    cleanup(path);
}

void testBackup() {
    Serial.println("\n--- FileStore Backup ---");

    const char* path = "/fst_bk";
    char tmp[32], bak[32];
    sidePath(tmp, path, 't');
    sidePath(bak, path, 'b');

    FileStore store(FileStore::Backup);

    // First write: no existing main
    cleanup(path);
    File f = store.open(LittleFS, path, "w");
    bool opened = (bool)f;
    bool wrote = opened && f.write((const uint8_t*)"FIRST", 5) == 5;
    bool closed = opened && store.close(LittleFS, path, f, wrote);

    check(closed &&
              fileEquals(path, "FIRST") &&
              !LittleFS.exists(tmp) &&
              !LittleFS.exists(bak),
          "Backup first write");

    // Replace existing file
    cleanup(path);
    writeText(path, "OLD");

    f = store.open(LittleFS, path, "w");
    opened = (bool)f;
    wrote = opened && f.write((const uint8_t*)"NEW", 3) == 3;
    closed = opened && store.close(LittleFS, path, f, wrote);

    check(closed &&
              fileEquals(path, "NEW") &&
              !LittleFS.exists(tmp) &&
              !LittleFS.exists(bak),
          "Backup commit existing main");

    // Failed write
    cleanup(path);
    writeText(path, "OLD");
    f = store.open(LittleFS, path, "w");
    opened = (bool)f;
    if (opened) f.write((const uint8_t*)"BADNEW", 6);
    closed = opened && store.close(LittleFS, path, f, false);

    check(!closed &&
              fileEquals(path, "OLD") &&
              !LittleFS.exists(tmp) &&
              !LittleFS.exists(bak),
          "Backup close(false) preserves main");

    // Unsupported mutating modes
    File fa = store.open(LittleFS, path, "a");
    File frp = store.open(LittleFS, path, "r+");
    check(!fa && !frp && fileEquals(path, "OLD"),
          "Backup rejects a and r+");

    cleanup(path);
}

void testBackupRecoveryMatrix() {
    Serial.println("\n--- Backup recovery matrix (27 states) ---");

    const char* path = "/fst_bm";
    char tmp[32], bak[32];
    sidePath(tmp, path, 't');
    sidePath(bak, path, 'b');

    FileStore store(FileStore::Backup);

    for (uint8_t mainState = 0; mainState < 3; ++mainState) {
        for (uint8_t tmpState = 0; tmpState < 3; ++tmpState) {
            for (uint8_t bakState = 0; bakState < 3; ++bakState) {
                cleanup(path);

                makeState(path, mainState, "OK:M", "BAD:M");
                makeState(tmp, tmpState, "OK:T", "BAD:T");
                makeState(bak, bakState, "OK:B", "BAD:B");

                bool ok = store.recover(LittleFS, path, validText);

                uint8_t expectedState = mainState;
                const char* expectedValid = "OK:M";
                const char* expectedInvalid = "BAD:M";

                if (tmpState || bakState) {
                    if (mainState == 1) {
                        expectedState = 1;
                        expectedValid = "OK:M";
                    } else if (tmpState == 1) {
                        expectedState = 1;
                        expectedValid = "OK:T";
                    } else if (bakState == 1) {
                        expectedState = 1;
                        expectedValid = "OK:B";
                    }
                }

                bool result = ok &&
                              expectedMain(path, expectedState, expectedValid, expectedInvalid) &&
                              !LittleFS.exists(tmp) &&
                              !LittleFS.exists(bak);

                char name[72];
                snprintf(name, sizeof(name),
                         "Backup recovery main=%u tmp=%u bak=%u",
                         mainState, tmpState, bakState);
                check(result, name);
            }
        }
    }

    cleanup(path);
}

void testRemoveAndFactory() {
    Serial.println("\n--- remove / factory / parallel paths ---");

    // remove() removes all transactional state
    const char* path = "/fst_rm";
    char tmp[32], bak[32];
    sidePath(tmp, path, 't');
    sidePath(bak, path, 'b');

    cleanup(path);
    writeText(path, "MAIN");
    writeText(tmp, "TMP");
    writeText(bak, "BAK");

    FileStore backup(FileStore::Backup);
    bool removed = backup.remove(LittleFS, path);

    check(removed &&
              !LittleFS.exists(path) &&
              !LittleFS.exists(tmp) &&
              !LittleFS.exists(bak),
          "Backup remove clears main/tmp/bak");

    // factory wrapper
    cleanup(path);
    auto store = makeFileStore(LittleFS, FileStore::Atomic);
    File f = store.open(path, "w");
    bool opened = (bool)f;
    bool wrote = opened && f.write((const uint8_t*)"FACTORY", 7) == 7;
    bool closed = opened && store.close(path, f, wrote);

    check(closed && fileEquals(path, "FACTORY"),
          "makeFileStore wrapper");

    // Different logical paths may be open simultaneously
    const char* a = "/fst_pa";
    const char* b = "/fst_pb";
    cleanup(a);
    cleanup(b);

    FileStore atomic(FileStore::Atomic);
    File fa = atomic.open(LittleFS, a, "w");
    File fb = atomic.open(LittleFS, b, "w");

    bool bothOpen = fa && fb;
    bool oka = bothOpen && fa.write((const uint8_t*)"A", 1) == 1;
    bool okb = bothOpen && fb.write((const uint8_t*)"B", 1) == 1;

    bool ca = bothOpen && atomic.close(LittleFS, a, fa, oka);
    bool cb = bothOpen && atomic.close(LittleFS, b, fb, okb);

    check(bothOpen && ca && cb &&
              fileEquals(a, "A") &&
              fileEquals(b, "B"),
          "Parallel transactions on different paths");

    cleanup(path);
    cleanup(a);
    cleanup(b);
}

void testPathRules() {
    Serial.println("\n--- path rules ---");

    FileStore direct;
    FileStore atomic(FileStore::Atomic);

    char p29[30];
    char p30[31];

    p29[0] = '/';
    for (uint8_t i = 1; i < 29; ++i) p29[i] = 'a';
    p29[29] = 0;

    p30[0] = '/';
    for (uint8_t i = 1; i < 30; ++i) p30[i] = 'a';
    p30[30] = 0;

    check(atomic.valid(p29), "Atomic accepts 29-char base path");
    check(!atomic.valid(p30), "Atomic rejects 30-char base path");
    check(direct.valid(p30), "Direct does not need suffix path limit");
}

struct __attribute__((packed)) Config {
    int32_t value;
    uint8_t enabled;
};

struct __attribute__((packed)) GrowV1 {
    int32_t a;
    uint8_t b;
};

struct __attribute__((packed)) GrowV2 {
    int32_t a;
    uint8_t b;
    uint16_t c;
};

void testSaverBasic() {
    Serial.println("\n--- SaverFile basic ---");

    const char* path = "/sv_basic";
    cleanup(path);

    Config cfg = {123, 1};
    SaverFile saver(LittleFS, path, cfg, 'A', 0, FileStore::Backup);

    check(saver.begin() == Saver::Default &&
              cfg.value == 123 &&
              cfg.enabled == 1,
          "Saver first begin -> Default");

    cfg.value = 456;
    cfg.enabled = 0;

    check(saver.write() == Saver::Write,
          "Saver manual write");

    Config loaded = {-1, 1};
    SaverFile reader(LittleFS, path, loaded, 'A', 0, FileStore::Backup);

    check(reader.begin() == Saver::Read &&
              loaded.value == 456 &&
              loaded.enabled == 0,
          "Saver second begin -> Read");

    // Version mismatch -> RAM defaults become new stored defaults
    Config verDefaults = {777, 1};
    SaverFile verSaver(LittleFS, path, verDefaults, 'B', 0, FileStore::Backup);

    check(verSaver.begin() == Saver::Default &&
              verDefaults.value == 777 &&
              verDefaults.enabled == 1,
          "Saver version mismatch -> Default");

    // Corrupt main and verify defaults are written
    writeText(path, "BAD");
    Config corruptDefaults = {888, 0};
    SaverFile corruptSaver(LittleFS, path, corruptDefaults, 'B', 0, FileStore::Backup);

    check(corruptSaver.begin() == Saver::Default &&
              corruptDefaults.value == 888 &&
              corruptDefaults.enabled == 0,
          "Saver corrupted file -> Default");

    cleanup(path);
}

void testSaverGrow() {
    Serial.println("\n--- SaverFile grow ---");

    const char* path = "/sv_grow";
    cleanup(path);

    GrowV1 oldCfg = {111, 1};
    SaverFile oldSaver(LittleFS, path, oldCfg, 'A', 0, FileStore::Backup);

    bool first = oldSaver.begin() == Saver::Default;
    oldCfg.a = 222;
    oldCfg.b = 0;
    bool saved = oldSaver.write() == Saver::Write;

    GrowV2 newCfg = {-1, 1, 0xBEEF};
    SaverFile newSaver(LittleFS, path, newCfg, 'A', 0, FileStore::Backup);

    bool grown = newSaver.beginGrow() == Saver::Grow;

    check(first && saved && grown &&
              newCfg.a == 222 &&
              newCfg.b == 0 &&
              newCfg.c == 0xBEEF,
          "Saver grow keeps old prefix and new tail defaults");

    GrowV2 reread = {0, 0, 0};
    SaverFile reader(LittleFS, path, reread, 'A', 0, FileStore::Backup);

    check(reader.begin() == Saver::Read &&
              reread.a == 222 &&
              reread.b == 0 &&
              reread.c == 0xBEEF,
          "Saver grown block is persisted");

    cleanup(path);
}

void testSaverReset() {
    Serial.println("\n--- SaverFile reset ---");

    const char* path = "/sv_reset";
    cleanup(path);

    Config cfg = {10, 1};
    SaverFile saver(LittleFS, path, cfg, 'A', 0, FileStore::Backup);
    bool begun = saver.begin() == Saver::Default;

    Config defaults = {42, 0};
    bool immediate = saver.reset(defaults) == Saver::Write &&
                     cfg.value == 42 &&
                     cfg.enabled == 0;

    Config loaded = {-1, 1};
    SaverFile reader(LittleFS, path, loaded, 'A', 0, FileStore::Backup);
    bool reread = reader.begin() == Saver::Read &&
                  loaded.value == 42 &&
                  loaded.enabled == 0;

    check(begun && immediate && reread,
          "Saver reset(data) updates RAM and storage");

    cfg.value = 99;
    cfg.enabled = 1;
    saver.write();

    bool invalidated = saver.reset() == Saver::Write;

    Config bootDefaults = {7, 1};
    SaverFile afterReset(LittleFS, path, bootDefaults, 'A', 0, FileStore::Backup);
    bool defaulted = afterReset.begin() == Saver::Default &&
                     bootDefaults.value == 7 &&
                     bootDefaults.enabled == 1;

    check(invalidated && defaulted,
          "Saver reset() removes storage; next begin uses RAM defaults");

    cleanup(path);
}

void testSaverAtomicMode() {
    Serial.println("\n--- SaverFile Atomic integration ---");

    const char* path = "/sv_atom";
    cleanup(path);

    Config cfg = {1, 1};
    SaverFile saver(LittleFS, path, cfg, 'A', 0, FileStore::Atomic);
    bool begun = saver.begin() == Saver::Default;

    cfg.value = 321;
    bool saved = saver.write() == Saver::Write;

    Config loaded = {0, 0};
    SaverFile reader(LittleFS, path, loaded, 'A', 0, FileStore::Atomic);
    bool read = reader.begin() == Saver::Read && loaded.value == 321;

    check(begun && saved && read,
          "Saver works with Atomic mode");

    cleanup(path);
}

void testSaverBlocks() {
    Serial.println("\n--- SaverFile multiple blocks ---");

    const char* path = "/sv_blocks";
    cleanup(path);

    Config cfg = {123, 1};
    uint16_t counter = 50;
    uint8_t flags[3] = {1, 2, 3};
    SaverBlock blocks[] = {saverBlock(cfg), saverBlock(counter), saverBlock(flags)};
    SaverFile saver(LittleFS, path, saverBlocks(blocks));
    const uint16_t total = sizeof(cfg) + sizeof(counter) + sizeof(flags);

    check(saver.dataSize() == total && saver.begin() == Saver::Default,
          "Blocks saverBlocks(array) and first begin");

    cfg.value = 456;
    counter = 789;
    flags[0] = 4;
    flags[2] = 6;
    check(saver.write() == Saver::Write && saver.write() == Saver::None,
          "Blocks write and unchanged detection");

    Config readCfg = {-1, 0};
    uint16_t readCounter = 0;
    uint8_t readFlags[3] = {};
    const SaverBlock readBlocks[] = {saverBlock(readCfg), saverBlock(readCounter), saverBlock(readFlags)};
    SaverFile reader(LittleFS, path, saverBlocks(readBlocks));
    check(reader.begin() == Saver::Read && readCfg.value == 456 && readCfg.enabled == 1 &&
              readCounter == 789 && readFlags[0] == 4 && readFlags[1] == 2 && readFlags[2] == 6,
          "Blocks read into const descriptor array");

    // блочная запись должна совпадать с обычным непрерывным форматом
    uint8_t flat[sizeof(cfg) + sizeof(counter) + sizeof(flags)] = {};
    SaverFile flatReader(LittleFS, path, (void*)flat, sizeof(flat));
    uint8_t expected[sizeof(flat)];
    memcpy(expected, &cfg, sizeof(cfg));
    memcpy(expected + sizeof(cfg), &counter, sizeof(counter));
    memcpy(expected + sizeof(cfg) + sizeof(counter), flags, sizeof(flags));
    check(flatReader.begin() == Saver::Read && memcmp(flat, expected, sizeof(flat)) == 0,
          "Blocks binary format matches contiguous data");

    // reset(raw) раскладывает один буфер обратно по разным участкам RAM
    uint8_t defaults[sizeof(flat)] = {};
    Config defaultCfg = {42, 0};
    uint16_t defaultCounter = 1000;
    uint8_t defaultFlags[3] = {7, 8, 9};
    memcpy(defaults, &defaultCfg, sizeof(defaultCfg));
    memcpy(defaults + sizeof(defaultCfg), &defaultCounter, sizeof(defaultCounter));
    memcpy(defaults + sizeof(defaultCfg) + sizeof(defaultCounter), defaultFlags, sizeof(defaultFlags));
    check(saver.reset(defaults, sizeof(defaults)) == Saver::Write &&
              cfg.value == 42 && cfg.enabled == 0 && counter == 1000 &&
              memcmp(flags, defaultFlags, sizeof(flags)) == 0,
          "Blocks reset(raw) scatters data into RAM");

    Config verifyCfg = {0, 1};
    uint16_t verifyCounter = 0;
    uint8_t verifyFlags[3] = {};
    SaverBlock verifyBlocks[] = {saverBlock(verifyCfg), saverBlock(verifyCounter), saverBlock(verifyFlags)};
    SaverFile verify(LittleFS, path, saverBlocks(verifyBlocks, 3), 'A', 0);
    check(verify.begin() == Saver::Read && verifyCfg.value == 42 && verifyCfg.enabled == 0 &&
              verifyCounter == 1000 && memcmp(verifyFlags, defaultFlags, sizeof(verifyFlags)) == 0,
          "Blocks saverBlocks(array, count) and reset persistence");

    cleanup(path);
}

void testSaverBlocksGrow() {
    Serial.println("\n--- SaverFile blocks grow ---");

    const char* path = "/sv_bgrow";
    cleanup(path);

    uint16_t a = 1234;
    uint8_t b[2] = {5, 6};
    SaverBlock oldBlocks[] = {saverBlock(a), saverBlock(b)};
    SaverFile oldSaver(LittleFS, path, saverBlocks(oldBlocks));
    bool initialized = oldSaver.begin() == Saver::Default;
    a = 4321;
    b[0] = 9;
    bool saved = oldSaver.write() == Saver::Write;

    uint16_t newA = 0;
    uint8_t newB[2] = {};
    uint32_t tail = 0x12345678;
    SaverBlock newBlocks[] = {saverBlock(newA), saverBlock(newB), saverBlock(tail)};
    SaverFile grown(LittleFS, path, saverBlocks(newBlocks));
    check(initialized && saved && grown.beginGrow() == Saver::Grow &&
              newA == 4321 && newB[0] == 9 && newB[1] == 6 && tail == 0x12345678,
          "Blocks beginGrow appends a new block");

    uint16_t readA = 0;
    uint8_t readB[2] = {};
    uint32_t readTail = 0;
    SaverBlock readBlocks[] = {saverBlock(readA), saverBlock(readB), saverBlock(readTail)};
    SaverFile reader(LittleFS, path, saverBlocks(readBlocks));
    check(reader.begin() == Saver::Read && readA == 4321 && readB[0] == 9 &&
              readB[1] == 6 && readTail == 0x12345678,
          "Blocks grown layout persists");

    cleanup(path);
}

void testSaverBlocksInvalid() {
    Serial.println("\n--- SaverFile invalid blocks ---");

    const char* path = "/sv_binvalid";
    cleanup(path);

    uint8_t value = 1;
    SaverBlock nullBlock[] = {{nullptr, 1}, saverBlock(value)};
    SaverFile invalidPointer(LittleFS, path, saverBlocks(nullBlock));
    check(invalidPointer.begin() == Saver::Error && !LittleFS.exists(path),
          "Blocks reject null data pointer");

    SaverBlock emptyBlock[] = {{&value, 0}};
    SaverFile invalidSize(LittleFS, path, saverBlocks(emptyBlock));
    check(invalidSize.begin() == Saver::Error && !LittleFS.exists(path),
          "Blocks reject zero-sized descriptor");

    SaverBlock oversized[] = {{&value, UINT16_MAX}, {&value, 1}};
    SaverFile invalidTotal(LittleFS, path, saverBlocks(oversized));
    check(invalidTotal.begin() == Saver::Error && !LittleFS.exists(path),
          "Blocks reject total size overflow");

    cleanup(path);
}

void testSaverTickGate() {
    Serial.println("\n--- SaverFile tick(allowWrite) ---");
    Serial.println("This test takes about 3-4 seconds.");

    const char* path = "/sv_tick";
    cleanup(path);

    Config cfg = {100, 1};
    SaverFile saver(LittleFS, path, cfg, 'A', 2, FileStore::Backup);

    if (saver.begin() != Saver::Default) {
        check(false, "Saver tick setup");
        cleanup(path);
        return;
    }

    cfg.value = 200;

    bool wroteWhileBlocked = false;
    uint32_t start = millis();

    while ((uint32_t)(millis() - start) < 2600) {
        if (saver.tick(false) == Saver::Write) wroteWhileBlocked = true;
        delay(20);
    }

    Config storedBefore = {0, 0};
    SaverFile beforeReader(LittleFS, path, storedBefore, 'A', 0, FileStore::Backup);
    bool stillOld = beforeReader.begin() == Saver::Read &&
                    storedBefore.value == 100;

    bool wroteAfterAllow = false;
    start = millis();

    while ((uint32_t)(millis() - start) < 1600) {
        if (saver.tick(true) == Saver::Write) {
            wroteAfterAllow = true;
            break;
        }
        delay(20);
    }

    Config storedAfter = {0, 0};
    SaverFile afterReader(LittleFS, path, storedAfter, 'A', 0, FileStore::Backup);
    bool nowNew = afterReader.begin() == Saver::Read &&
                  storedAfter.value == 200;

    check(!wroteWhileBlocked && stillOld && wroteAfterAllow && nowNew,
          "tick(false) delays physical write; tick(true) later commits");

    cleanup(path);
}

void cleanupAll() {
    const char* paths[] = {
        "/fst_dir", "/fst_at", "/fst_am", "/fst_bk", "/fst_bm",
        "/fst_rm", "/fst_pa", "/fst_pb",
        "/sv_basic", "/sv_grow", "/sv_reset", "/sv_atom", "/sv_tick",
        "/sv_blocks", "/sv_bgrow", "/sv_binvalid"};

    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        cleanup(paths[i]);
    }
}

#ifdef ESP32
#define FORMAT_ON_FAIL true
#else
#define FORMAT_ON_FAIL
#endif

void setup() {
    Serial.begin(115200);
    delay(1200);

    Serial.println();
    Serial.println("========================================");
    Serial.println(" FileStore + SaverFile hardware test");
    Serial.println("========================================");
    Serial.println("Uses /fst_* and /sv_* test files only.");
    Serial.println("LittleFS.begin(true): format on mount failure is enabled.");

    if (!LittleFS.begin(FORMAT_ON_FAIL)) {
        Serial.println("\n[FATAL] LittleFS.begin(true) failed");
        return;
    }

    cleanupAll();

    testDirect();
    testAtomic();
    testAtomicRecoveryMatrix();
    testBackup();
    testBackupRecoveryMatrix();
    testRemoveAndFactory();
    testPathRules();

    testSaverBasic();
    testSaverGrow();
    testSaverReset();
    testSaverAtomicMode();
    testSaverBlocks();
    testSaverBlocksGrow();
    testSaverBlocksInvalid();
    testSaverTickGate();

    cleanupAll();

    Serial.println();
    Serial.println("========================================");
    Serial.print("PASS: ");
    Serial.println(g_pass);
    Serial.print("FAIL: ");
    Serial.println(g_fail);
    Serial.println("----------------------------------------");

    if (!g_fail) Serial.println("ALL TESTS PASSED");
    else Serial.println("SOME TESTS FAILED");

    Serial.println("========================================");
}

void loop() {
}
