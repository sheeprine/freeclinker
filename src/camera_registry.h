#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <string>

static constexpr uint8_t  CAMREG_MAX      = 64;
static constexpr uint8_t  CAMREG_NAME_LEN = 30;
static constexpr uint8_t  CAMREG_ADDR_LEN = 18;  // "aa:bb:cc:dd:ee:ff\0"

// Stored as a packed binary blob in NVS.
// All fields are byte-aligned — no padding.
struct CameraEntry {
    char    name[CAMREG_NAME_LEN];  // display name seen during scan
    char    addr[CAMREG_ADDR_LEN];  // BLE address string
    uint8_t addrType;               // esp_ble_addr_type_t (0=public, 1=random)
    uint8_t cameraType;             // 0=DJI, 1=GoPro, 2=Caddx (unused today — Caddx
                                     // has no BLE pairing flow, so it never populates
                                     // this registry; kept for display consistency)
};

// Persists a list of up to 64 previously connected cameras to NVS.
// Remembers the last successfully connected camera and supports selecting
// a specific camera for the next connection attempt.
class CameraRegistry {
public:
    void begin();

    // Called after a successful BLE camera connection.
    // Adds or updates the entry and marks it as the last connected camera.
    // Clears any manual selection.
    void onConnected(const char *name, const char *addr,
                     uint8_t addrType, uint8_t cameraType);

    // Returns the BLE address to prefer during the next scan.
    // A manually selected camera takes priority over the last connected.
    // Returns an empty string if no preference is set.
    std::string preferredAddr()     const;
    uint8_t     preferredAddrType() const;

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
