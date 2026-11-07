#include <algorithm>
#include <array>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <emscripten/bind.h>

#include "esphome/components/wmbus_common/meter.h"
#include "esphome/components/wmbus_common/telegram.h"

namespace {

using emscripten::val;

template<typename Bytes> std::optional<Bytes> parse_hex(std::string_view hex) {
  Bytes out{};
  if constexpr (requires { out.resize(0); })
    out.resize(hex.size() / 2);
  if (hex.size() != 2 * out.size())
    return {};
  for (size_t i = 0; i < out.size(); i++) {
    const char *at = hex.data() + 2 * i;
    if (std::from_chars(at, at + 2, out[i], 16).ptr != at + 2)
      return {};
  }
  return out;
}

// By wmbus::DecodeError.
constexpr const char *ERROR_CODES[] = {"", "unknown_driver", "no_key", "decryption_failed", "not_understood"};

val answer(const char *error, val json = val::null()) {
  val out = val::object();
  out.set("error", error);
  out.set("json", json);
  return out;
}

std::string text(const val &v, const char *fallback) { return v.isUndefined() ? fallback : v.as<std::string>(); }

// decode({driver = "auto", key, telegrams}) gives {error, json} for one meter's hex frames without link-layer CRCs.
val decode(const val &args) {
  const std::string driver = text(args["driver"], "auto");
  const std::string key = text(args["key"], "");

  std::vector<std::vector<uint8_t>> frames;
  for (const std::string &hex : emscripten::vecFromJSArray<std::string>(args["telegrams"])) {
    auto frame = parse_hex<std::vector<uint8_t>>(hex);
    if (!frame)
      return answer("not_a_telegram");
    frames.push_back(std::move(*frame));
  }
  wmbus::Telegram first;
  if (frames.empty() || !first.parse_envelope(frames.front()))
    return answer("not_a_telegram");

  const auto aes = key.empty() ? std::nullopt : parse_hex<std::array<uint8_t, 16>>(key);
  if (!key.empty() && !aes)
    return answer("bad_key");

  // "auto" leaves the spec null, as `type: auto` does.
  const wmbus::DriverSpec *spec = nullptr;
  if (driver != "auto") {
    const auto found =
        std::ranges::find_if(wmbus::registered_drivers, [&](const wmbus::DriverSpec *s) { return driver == s->name; });
    if (found == wmbus::registered_drivers.end())
      return answer("unknown_driver");
    spec = *found;
  }

  wmbus::MeterState meter(spec, first.sender().id, aes);
  for (const std::vector<uint8_t> &frame : frames)
    meter.handle_telegram(frame);
  return answer(ERROR_CODES[static_cast<size_t>(meter.error)], val::global("JSON").call<val>("parse", meter.to_json()));
}

}  // namespace

EMSCRIPTEN_BINDINGS(wmbusmeters_wasm) { emscripten::function("decode", &decode); }
