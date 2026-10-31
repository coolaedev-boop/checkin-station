#pragma once
// PC stand-in for the Arduino core: just what the UI and Inventory use.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>

using String = std::string;

uint32_t millis();
inline void delay(uint32_t) {}

struct SerialStub {
  void println(const char* s) { printf("[serial] %s\n", s); }
  template <typename... A> void printf(const char* f, A... a) { ::printf(f, a...); }
};
extern SerialStub Serial;
