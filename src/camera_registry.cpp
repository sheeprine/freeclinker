#include "camera_registry.h"
#include <cstring>

static constexpr const char *NVS_NS   = "cam_reg";
static constexpr const char *KEY_CNT  = "cnt";
static constexpr const char *KEY_LAST = "last";
static constexpr const char *KEY_LIST = "list";

static const char *cameraTypeName(uint8_t t) {
    switch (t) {
        case 1:  return "GoPro";
        case 2:  return "Caddx";
        case 3:  return "Sony";
        case 4:  return "Blackmagic";
        default: return "DJI";
    }
}

void CameraRegistry::begin() {
    _prefs.begin(NVS_NS, false);
    load();
}

void CameraRegistry::load() {
    _count   = _prefs.getUChar(KEY_CNT, 0);
    _lastIdx = (int8_t)_prefs.getChar(KEY_LAST, -1);

    if (_count > CAMREG_MAX) { _count = 0; _lastIdx = -1; return; }
    if (_lastIdx >= (int8_t)_count) _lastIdx = -1;

    if (_count > 0) {
        size_t expected = (size_t)_count * sizeof(CameraEntry);
        size_t got = _prefs.getBytes(KEY_LIST, _entries, expected);
        if (got != expected) { _count = 0; _lastIdx = -1; }
    }
}

void CameraRegistry::save() {
    _prefs.putUChar(KEY_CNT,  _count);
    _prefs.putChar (KEY_LAST, (char)_lastIdx);
    if (_count > 0)
        _prefs.putBytes(KEY_LIST, _entries, (size_t)_count * sizeof(CameraEntry));
    else
        _prefs.remove(KEY_LIST);
}

int CameraRegistry::findByAddr(const char *addr) const {
    for (int i = 0; i < (int)_count; i++)
        if (strcmp(_entries[i].addr, addr) == 0) return i;
    return -1;
}

void CameraRegistry::onConnected(const char *name, const char *addr,
                                   uint8_t addrType, uint8_t cameraType,
                                   const char *pass) {
    int idx = findByAddr(addr);
    const bool isNew = (idx < 0);
    if (isNew) {
        if (_count < CAMREG_MAX) {
            idx = (int)_count++;
        } else {
            // List full: evict oldest entry that isn't the last connected
            int evict = (_lastIdx == 0) ? 1 : 0;
            memmove(&_entries[evict], &_entries[evict + 1],
                    ((size_t)_count - (size_t)evict - 1) * sizeof(CameraEntry));
            _count--;
            if (_lastIdx > evict) _lastIdx--;
            idx = (int)_count++;
        }
    }
    strlcpy(_entries[idx].name, name, CAMREG_NAME_LEN);
    strlcpy(_entries[idx].addr, addr, CAMREG_ADDR_LEN);
    _entries[idx].addrType   = addrType;
    _entries[idx].cameraType = cameraType;
    if (pass)       strlcpy(_entries[idx].pass, pass, CAMREG_PASS_LEN);
    else if (isNew) _entries[idx].pass[0] = '\0';
    _lastIdx     = (int8_t)idx;
    _selectedIdx = -1;
    save();
}

std::string CameraRegistry::preferredAddr() const {
    int idx = (_selectedIdx >= 0) ? _selectedIdx : _lastIdx;
    if (idx < 0 || idx >= (int)_count) return "";
    return std::string(_entries[idx].addr);
}

uint8_t CameraRegistry::preferredAddrType() const {
    int idx = (_selectedIdx >= 0) ? _selectedIdx : _lastIdx;
    if (idx < 0 || idx >= (int)_count) return 0;
    return _entries[idx].addrType;
}

bool CameraRegistry::preferredEntry(uint8_t wantType, CameraEntry &out) const {
    int idx = (_selectedIdx >= 0) ? _selectedIdx : _lastIdx;
    if (idx < 0 || idx >= (int)_count) return false;
    if (_entries[idx].cameraType != wantType) return false;
    out = _entries[idx];
    return true;
}

bool CameraRegistry::setPassword(uint8_t idx, const char *pass) {
    if (idx >= _count) return false;
    strlcpy(_entries[idx].pass, pass, CAMREG_PASS_LEN);
    save();
    return true;
}

void CameraRegistry::selectCamera(uint8_t idx) {
    if (idx < _count) _selectedIdx = (int8_t)idx;
}

void CameraRegistry::clearSelection() {
    _selectedIdx = -1;
}

bool CameraRegistry::getEntry(uint8_t idx, CameraEntry &out) const {
    if (idx >= _count) return false;
    out = _entries[idx];
    return true;
}

bool CameraRegistry::remove(uint8_t idx) {
    if (idx >= _count) return false;
    memmove(&_entries[idx], &_entries[idx + 1],
            ((size_t)_count - idx - 1) * sizeof(CameraEntry));
    _count--;
    if (_lastIdx    == (int8_t)idx) _lastIdx = -1;
    else if (_lastIdx    > (int8_t)idx) _lastIdx--;
    if (_selectedIdx == (int8_t)idx) _selectedIdx = -1;
    else if (_selectedIdx > (int8_t)idx) _selectedIdx--;
    save();
    return true;
}

void CameraRegistry::clear() {
    _count       = 0;
    _lastIdx     = -1;
    _selectedIdx = -1;
    save();
}

void CameraRegistry::printList(Stream &out) const {
    if (_count == 0) { out.println("[reg] No cameras saved"); return; }
    out.printf("[reg] %u camera(s) saved:\n", _count);
    for (uint8_t i = 0; i < _count; i++) {
        const char *tag = "";
        bool isLast = (i == (uint8_t)_lastIdx);
        bool isSel  = (i == (uint8_t)_selectedIdx);
        if (isLast && isSel)  tag = " [last+sel]";
        else if (isLast)      tag = " [last]";
        else if (isSel)       tag = " [selected]";
        // Two literal spaces (not one) before addr: %-28s only pads up to
        // its width, so a name at or past 28 chars (real Caddx device
        // names routinely are) would otherwise leave just one space —
        // web/index.html's parser needs a guaranteed 2+-space run to find
        // the field boundary, same as it already gets before the type.
        out.printf("[reg]  %2u: %-28s  %s  %s%s\n", i,
                   _entries[i].name, _entries[i].addr,
                   cameraTypeName(_entries[i].cameraType), tag);
    }
}

String CameraRegistry::toJson() const {
    String out = "[";
    for (uint8_t i = 0; i < _count; i++) {
        if (i) out += ',';
        out += F("{\"idx\":");      out += i;
        out += F(",\"name\":\"");   out += _entries[i].name;
        out += F("\",\"addr\":\""); out += _entries[i].addr;
        out += F("\",\"type\":");   out += _entries[i].cameraType;
        out += F(",\"last\":");     out += (i == (uint8_t)_lastIdx    ? F("true") : F("false"));
        out += F(",\"sel\":");      out += (i == (uint8_t)_selectedIdx ? F("true") : F("false"));
        out += '}';
    }
    out += ']';
    return out;
}
