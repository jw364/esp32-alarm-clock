// NetworkClient.h shim for Arduino ESP32 core 2.x.
// In core 3.x, NetworkClient is a renamed/refactored WiFiClient base class.
// On core 2.x, WiFiClient provides the same interface under a different name.
// ESP32-audioI2S v3.x includes <NetworkClient.h>; this shim satisfies that.
#pragma once
#include <WiFiClient.h>

// Make NetworkClient an alias for WiFiClient for core 2.x
using NetworkClient = WiFiClient;
