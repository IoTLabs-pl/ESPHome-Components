#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>

namespace esphome {

inline std::string __attribute__((format(printf, 1, 2))) str_sprintf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  const int length = vsnprintf(nullptr, 0, fmt, args);
  va_end(args);

  std::string out(length, '\0');
  va_start(args, fmt);
  vsnprintf(out.data(), length + 1, fmt, args);
  va_end(args);
  return out;
}

}  // namespace esphome
