#include "oui_lookup.h"
#include <cstring>

struct OuiEntry {
    uint32_t oui24;
    const char* vendor;
};

// Sorted by oui24 ascending
static const OuiEntry OUI_TABLE[] = {
    {0x000393, "Apple"},
    {0x000B86, "Aruba"},
    {0x000D93, "Roku"},
    {0x000E08, "Cisco"},
    {0x001124, "Samsung"},
    {0x001A11, "Google"},
    {0x001D7E, "Cisco"},
    {0x002191, "Dell"},
    {0x0022FA, "Cisco"},
    {0x0024D2, "Intel"},
    {0x00259C, "NETGEAR"},
    {0x0026F2, "Logitech"},
    {0x003192, "Belkin"},
    {0x004096, "Cisco"},
    {0x0050F2, "Microsoft"},
    {0x0060B3, "Hewlett Packard"},
    {0x008865, "Apple"},
    {0x00A0D1, "Inventec"},
    {0x00E04C, "Realtek"},
    {0x0418D6, "Ubiquiti"},
    {0x048D38, "Netgear"},
    {0x08606E, "ASUSTek"},
    {0x0C8063, "TP-Link"},
    {0x10DA43, "Nest"},
    {0x14CC20, "TP-Link"},
    {0x18B430, "Nest"},
    {0x1C3BF3, "TP-Link"},
    {0x1C61B4, "TP-Link"},
    {0x1C9DC2, "Espressif"},
    {0x1CC0E1, "Amazon"},
    {0x20F17C, "Huawei"},
    {0x24A43C, "Cisco"},
    {0x24E4C8, "Amazon"},
    {0x28E98E, "Apple"},
    {0x2C3AE8, "Espressif"},
    {0x2CAA8E, "Wyze"},
    {0x2CF0A2, "Apple"},
    {0x30AEA4, "Espressif"},
    {0x34E894, "Sonos"},
    {0x38F9D3, "Apple"},
    {0x3C6105, "JetBrains"},
    {0x3C52A1, "Hewlett Packard"},
    {0x40ED98, "Amazon"},
    {0x44D9E7, "Ubiquiti"},
    {0x4844F7, "Samsung"},
    {0x48D705, "Apple"},
    {0x4C8D79, "Apple"},
    {0x50D4F7, "TP-Link"},
    {0x544E90, "Apple"},
    {0x54E61B, "Apple"},
    {0x5CAAFD, "Sonos"},
    {0x5CF938, "Apple"},
    {0x6038E0, "Belkin"},
    {0x60A4B7, "TP-Link"},
    {0x60F189, "Apple"},
    {0x64B473, "Apple"},
    {0x68AB8A, "Roku"},
    {0x6C4A85, "Apple"},
    {0x6C96CF, "Apple"},
    {0x708BCD, "ASUSTek"},
    {0x70F087, "Apple"},
    {0x74AC5D, "Qnap"},
    {0x7824AF, "ASUSTek"},
    {0x7C2EDD, "Samsung"},
    {0x803A0A, "Integrated Device"},
    {0x80EA96, "Apple"},
    {0x84FCFE, "Apple"},
    {0x88C663, "Hewlett Packard"},
    {0x8C8590, "Apple"},
    {0x90B21F, "Apple"},
    {0x94E9EE, "Amazon"},
    {0x98F4AB, "Hewlett Packard"},
    {0x9C8E99, "Hewlett Packard"},
    {0xA0DE0F, "Huawei"},
    {0xA4CF12, "Espressif"},
    {0xA4E57C, "Apple"},
    {0xACDE48, "Apple"},
    {0xB0B98A, "Netgear"},
    {0xB4E62A, "LG"},
    {0xB827EB, "Raspberry Pi"},
    {0xB8E856, "Apple"},
    {0xBC6E64, "Sonos"},
    {0xC0D012, "Hikvision"},
    {0xC0F4E6, "Hikvision"},
    {0xC83A35, "Tuya Smart"},
    {0xCC3A61, "Samsung"},
    {0xD0D2B0, "Apple"},
    {0xD4F98D, "Apple"},
    {0xD8A25E, "August"},
    {0xDC4F22, "Espressif"},
    {0xE006E6, "Hewlett Packard"},
    {0xE4C63D, "Apple"},
    {0xE89F80, "Belkin"},
    {0xF01898, "Apple"},
    {0xF4F5D8, "Google"},
    {0xF4F5E8, "Google"},
    {0xF832E4, "ASUSTek"},
    {0xFCFC48, "Apple"},
};

static const size_t OUI_COUNT = sizeof(OUI_TABLE) / sizeof(OUI_TABLE[0]);

static uint32_t macOui24(const uint8_t mac[6]) {
    return ((uint32_t)mac[0] << 16) | ((uint32_t)mac[1] << 8) | mac[2];
}

const char* ouiLookupVendor(const uint8_t mac[6]) {
    if (!mac || (mac[0] == 0 && mac[1] == 0 && mac[2] == 0)) return "Unknown";
    uint32_t key = macOui24(mac);
    int lo = 0, hi = (int)OUI_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (OUI_TABLE[mid].oui24 == key) return OUI_TABLE[mid].vendor;
        if (OUI_TABLE[mid].oui24 < key) lo = mid + 1;
        else hi = mid - 1;
    }
    return "Unknown";
}
