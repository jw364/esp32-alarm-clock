// NetworkClientSecure.h shim for Arduino ESP32 core 2.x.
// In core 3.x NetworkClientSecure replaced WiFiClientSecure.
#pragma once
#include <WiFiClientSecure.h>
using NetworkClientSecure = WiFiClientSecure;
