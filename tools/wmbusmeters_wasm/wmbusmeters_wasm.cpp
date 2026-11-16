#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include <emscripten/bind.h>

#include "esphome/components/wmbus_common/meter.h"
#include "esphome/components/wmbus_common/telegram.h"
#include "esphome/core/helpers.h"

namespace {

using emscripten::val;

val answer(const char *error, bool encrypted = false, val json = val::null()) {
  val out = val::object();
  out.set("error", error);
  out.set("json", json);
  out.set("encrypted", encrypted);
  return out;
}

std::string text(const val &v, const char *fallback) { return v.isUndefined() ? fallback : v.as<std::string>(); }

bool is_encrypted(const wmbus::Telegram &t) {
  return (t.ell && t.ell->encrypted) || (t.tpl && t.tpl->sec_mode != wmbus::TPL_NO_SECURITY);
}

// decode({driver = "auto", key, telegrams}) gives {error, json, encrypted} for one meter's hex frames, no link CRCs.
val decode(const val &args) {
  const std::string driver = text(args["driver"], "auto");
  const std::string key = text(args["key"], "");

  std::vector<std::vector<uint8_t>> frames;
  for (const std::string &hex : emscripten::vecFromJSArray<std::string>(args["telegrams"])) {
    std::vector<uint8_t> frame;
    if (hex.size() % 2 || !esphome::parse_hex(hex, frame, hex.size() / 2))
      return answer("not_a_telegram");
    frames.push_back(std::move(frame));
  }
  wmbus::Telegram first;
  if (frames.empty() || !first.parse_envelope(frames.front()))
    return answer("not_a_telegram");
  const bool encrypted = is_encrypted(first);

  std::optional<std::array<uint8_t, 16>> aes;
  if (!key.empty()) {
    aes.emplace();
    if (key.size() != 2 * aes->size() || !esphome::parse_hex(key, aes->data(), aes->size()))
      return answer("bad_key", encrypted);
  }

  const wmbus::DriverSpec *spec = nullptr;
  if (driver != "auto") {
    const auto found =
        std::ranges::find_if(wmbus::registered_drivers, [&](const wmbus::DriverSpec *s) { return driver == s->name; });
    if (found == wmbus::registered_drivers.end())
      return answer("unknown_driver", encrypted);
    spec = *found;
  }

  wmbus::MeterState meter(spec, first.sender().id, aes);
  for (const std::vector<uint8_t> &frame : frames)
    meter.handle_telegram(frame);
  return answer(meter.last_error.name(), encrypted, val::global("JSON").call<val>("parse", meter.to_json()));
}

}  // namespace

EMSCRIPTEN_BINDINGS(wmbusmeters_wasm) { emscripten::function("decode", &decode); }
