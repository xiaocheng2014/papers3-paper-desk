#pragma once

#include <Arduino.h>

namespace paperdesk {

struct LiteracyEntry {
  const char* character;
  const char* pinyin;
  const char* words;
};

extern const LiteracyEntry kLiteracyEntries[];
extern const size_t kLiteracyEntryCount;

}  // namespace paperdesk
