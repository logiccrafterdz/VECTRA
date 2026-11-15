// Configuration loading: JSON file -> typed engine settings.
//
// Every threshold in the file is a starting value, not an established
// constant. Keys marked UNCALIBRATED in config/default.json have not been
// fitted to any data.
#pragma once

#include <string>

#include "vectra/backtest/replay_engine.hpp"
#include "vectra/config/json.hpp"

namespace vectra::config {

struct Settings {
  std::string instrument = "EURUSD";
  backtest::ReplayConfig replay;

  Settings() {
    replay.instrument = instrument;
  }
};

// Built-in defaults; also the fallback for missing keys.
Settings default_settings();

// Reads and parses a JSON config file, overlaying it onto the defaults.
// Returns false (and fills `error`) on I/O or parse errors.
bool load_settings(const std::string& path, Settings* out, std::string* error);

// Overlays an already-parsed JSON object onto `out`. Exposed for tests.
bool overlay_settings(const Json& root, Settings* out, std::string* error);

}  // namespace vectra::config
