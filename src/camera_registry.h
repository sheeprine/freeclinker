#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <string>

static constexpr uint8_t  CAMREG_MAX      = 64;
static constexpr uint8_t  CAMREG_NAME_LEN = 30;
// Sized to fit either a BLE address ("aa:bb:cc:dd:ee:ff\0", 18 bytes) or a
// Wi-Fi SSID (32 bytes max + NUL) — Caddx entries store their SSID here.
static constexpr uint8_t  CAMREG_ADDR_LEN = 33;
// WPA2 passphrase max is 63 chars + NUL. Unused (empty) for DJI/GoPro.
static constexpr uint8_t  CAMREG_PASS_LEN = 64;

// Stored as a packed binary blob in NVS.
// All fields are byte-aligned — no padding.
struct CameraEntry {
    char    name[CAMREG_NAME_LEN];  // display name seen during scan (or Caddx's own name)
    char    addr[CAMREG_ADDR_LEN];  // BLE address, or Wi-Fi SSID for Caddx entries
    uint8_t addrType;               // esp_ble_addr_type_t (0=public, 1=random); unused for Caddx
    uint8_t cameraType;             // 0=DJI, 1=GoPro, 2=Caddx, 3=Sony, 4=Blackmagic
    char    pass[CAMREG_PASS_LEN];  // Wi-Fi password, Caddx only — never printed/exported
};

// Persists a list of up to 64 previously connected cameras to NVS.
// Remembers the last successfully connected camera and supports selecting
// a specific camera for the next connection attempt.
class CameraRegistry {
public:
    void begin();

    // Called after a successful BLE camera connection, or (for Caddx, which
    // has no discovery step) after the user configures a Wi-Fi network that
    // successfully connects. Adds or updates the entry and marks it as the
    // last connected camera. Clears any manual selection.
    // `pass` is optional (BLE/GoPro never pass one): nullptr leaves an
    // existing entry's password untouched and defaults a new entry to
    // empty, so Caddx's own reconnect-confirmation call (which omits it)
    // never clobbers a password set separately via setPassword().
    void onConnected(const char *name, const char *addr, uint8_t addrType,
                     uint8_t cameraType, const char *pass = nullptr);

    // Returns the BLE address to prefer during the next scan.
    // A manually selected camera takes priority over the last connected.
    // Returns an empty string if no preference is set.
    std::string preferredAddr()     const;
    uint8_t     preferredAddrType() const;

    // Like preferredAddr()/preferredAddrType() combined, but scoped to a
    // specific cameraType — the registry is shared across camera types, so
    // Caddx (no discovery/fallback to correct a wrong guess) needs this
    // instead of blindly trusting whatever was last connected overall.
    bool preferredEntry(uint8_t wantType, CameraEntry &out) const;

    // Updates just the password of an existing entry (Caddx only).
    bool setPassword(uint8_t idx, const char *pass);

    // Manually select a camera by index for the next connection attempt.
    // The selection is volatile (not persisted) and is cleared after a
    // successful connection.
    void selectCamera(uint8_t idx);
    void clearSelection();
    bool hasSelection()   const { return _selectedIdx >= 0; }

    uint8_t count()       const { return _count; }
    int8_t  lastIdx()     const { return _lastIdx; }
    int8_t  selectedIdx() const { return _selectedIdx; }

    bool getEntry(uint8_t idx, CameraEntry &out) const;
    bool remove(uint8_t idx);
    void clear();

    void   printList(Stream &out) const;
    String toJson()               const;

private:
    CameraEntry _entries[CAMREG_MAX];
    uint8_t     _count       = 0;
    int8_t      _lastIdx     = -1;
    int8_t      _selectedIdx = -1;  // volatile — not persisted

    Preferences _prefs;

    void load();
    void save();
    int  findByAddr(const char *addr) const;
};
