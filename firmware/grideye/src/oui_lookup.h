#pragma once
#include <Arduino.h>

// IEEE OUI vendor hints (public MA-L registry, curated subset for embedded lookup)
const char* ouiLookupVendor(const uint8_t mac[6]);
