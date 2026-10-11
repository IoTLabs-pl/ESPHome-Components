#include "wmbus_test_runner.h"

#include "esphome/components/wmbus_common/meter.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <iostream>
#include <string>
#include <vector>

namespace esphome {
namespace wmbus_test_runner {

// Results share stdout with ESPHome's logs.
static const char *RESULT_PREFIX = "@@WMBUS@@ ";
static const char *ERROR_PREFIX = "@@WMBUS-ERR@@ ";

namespace {

const wmbus::DriverSpec *find_driver(const std::string &name) {
  for (const wmbus::DriverSpec *spec : wmbus::registered_drivers) {
    if (name == spec->name)
      return spec;
  }
  return nullptr;
}

std::vector<std::string> split(const std::string &line, char sep) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (true) {
    size_t at = line.find(sep, start);
    if (at == std::string::npos) {
      parts.push_back(line.substr(start));
      return parts;
    }
    parts.push_back(line.substr(start, at - start));
    start = at + 1;
  }
}

// driver<TAB>id<TAB>key<TAB>telegram[,telegram...]; key "NOKEY" means unencrypted, as upstream writes it.
void run_case(const std::string &line) {
  std::vector<std::string> args = split(line, '\t');
  if (args.size() != 4) {
    printf("%smalformed case\n", ERROR_PREFIX);
    return;
  }

  const std::string &driver = args[0];
  const std::string &id = args[1];

  std::optional<std::array<uint8_t, 16>> key;
  if (args[2] != "NOKEY") {
    key.emplace();
    if (args[2].size() != 2 * key->size() || !esphome::parse_hex(args[2], key->data(), key->size())) {
      printf("%sbad key %s\n", ERROR_PREFIX, args[2].c_str());
      return;
    }
  }

  // "auto" leaves the spec null, as `type: auto` does.
  const wmbus::DriverSpec *spec = nullptr;
  if (driver != "auto") {
    spec = find_driver(driver);
    if (spec == nullptr) {
      printf("%sunknown driver %s\n", ERROR_PREFIX, driver.c_str());
      return;
    }
  }

  wmbus::MeterState meter(spec, (uint32_t) strtoul(id.c_str(), nullptr, 16), key);
  bool any_match = false;
  for (const std::string &hex : split(args[3], ',')) {
    if (hex.empty())
      continue;

    std::vector<uint8_t> frame;
    if (!esphome::parse_hex(hex, frame, hex.size() / 2)) {
      printf("%sbad hex in telegram\n", ERROR_PREFIX);
      return;
    }

    if (meter.handle_telegram(frame) != wmbus::Result::NotForThisMeter)
      any_match = true;
  }

  if (!any_match) {
    printf("%sno telegram matched meter id %s\n", ERROR_PREFIX, id.c_str());
    return;
  }

  // Only `auto` gets here without a driver: no detect{} in the registry matched.
  if (meter.spec == nullptr) {
    printf("%s%s\n", ERROR_PREFIX, meter.last_error.c_str());
    return;
  }

  printf("%s%s\t%s\n", RESULT_PREFIX, meter.spec->name, meter.to_json().c_str());
}

}  // namespace

void TestRunner::setup() {
  setenv("TZ", "UTC", 1);
  tzset();
  std::string line;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty())
      continue;
    run_case(line);
  }
  fflush(stdout);
  std::exit(0);
}

}  // namespace wmbus_test_runner
}  // namespace esphome
