// Configuration tests: strict JSON parsing and settings overlay.
#include "vectra_test.hpp"

#include <fstream>
#include <string>

#include "vectra/config/json.hpp"
#include "vectra/config/settings.hpp"

using vectra::config::Json;
using vectra::config::Settings;

VT_TEST(Json, ParsesNestedObjectsAndTypedValues) {
  std::string error;
  const std::string text = R"({
    "instrument": "EURUSD",
    "risk": { "kill_switch": true, "max_daily_loss": 250.5, "orders": 7 },
    "windows": [1, 2, 3],
    "tiny": 1e-8,
    "negative": -0.00015
  })";
  auto root = Json::parse(text, &error);
  VT_ASSERT_TRUE(root.has_value());
  VT_ASSERT_TRUE(error.empty());
  VT_ASSERT_TRUE(root->is_object());
  VT_ASSERT_STREQ(root->get_string("instrument", ""), std::string("EURUSD"));

  const Json* risk = root->get_object("risk");
  VT_ASSERT_TRUE(risk != nullptr);
  VT_ASSERT_TRUE(risk->get_bool("kill_switch", false));
  VT_ASSERT_NEAR(risk->get_number("max_daily_loss", 0.0), 250.5, 1e-12);
  VT_ASSERT_EQ(risk->get_size("orders", 0), std::size_t{7});

  const Json* windows = root->find("windows");
  VT_ASSERT_TRUE(windows != nullptr);
  VT_ASSERT_TRUE(windows->is_array());
  VT_ASSERT_EQ(windows->size(), std::size_t{3});
  VT_ASSERT_NEAR(windows->at(1).number_or(0.0), 2.0, 1e-12);
  VT_ASSERT_TRUE(windows->at(9).is_null());  // out-of-range access is null

  VT_ASSERT_NEAR(root->get_number("tiny", 0.0), 1e-8, 1e-18);
  VT_ASSERT_NEAR(root->get_number("negative", 0.0), -0.00015, 1e-18);
}

VT_TEST(Json, MissingKeysFallBackToDefaults) {
  std::string error;
  auto root = Json::parse("{}", &error);
  VT_ASSERT_TRUE(root.has_value());
  VT_ASSERT_NEAR(root->get_number("nope", 42.0), 42.0, 1e-12);
  VT_ASSERT_FALSE(root->get_bool("nope", false));
  VT_ASSERT_STREQ(root->get_string("nope", "fallback"), std::string("fallback"));
  VT_ASSERT_TRUE(root->get_object("nope") == nullptr);
}

VT_TEST(Json, RejectsMalformedInputWithPosition) {
  std::string error;
  auto bad = Json::parse("{ \"a\": }", &error);
  VT_ASSERT_FALSE(bad.has_value());
  VT_ASSERT_FALSE(error.empty());

  error.clear();
  auto trailing = Json::parse("{} extra", &error);
  VT_ASSERT_FALSE(trailing.has_value());
  VT_ASSERT_FALSE(error.empty());

  error.clear();
  auto unterminated = Json::parse("{ \"a\": \"oops }", &error);
  VT_ASSERT_FALSE(unterminated.has_value());
}

VT_TEST(Json, RejectsNonObjectTopLevelForSettings) {
  Settings settings;
  std::string error;
  auto root = Json::parse("[1,2,3]", &error);
  VT_ASSERT_TRUE(root.has_value());
  VT_ASSERT_FALSE(vectra::config::overlay_settings(*root, &settings, &error));
  VT_ASSERT_FALSE(error.empty());
}

VT_TEST(Settings, DefaultsAreSane) {
  const Settings settings = vectra::config::default_settings();
  VT_ASSERT_STREQ(settings.instrument, std::string("EURUSD"));
  VT_ASSERT_TRUE(settings.replay.risk.enabled);
  VT_ASSERT_FALSE(settings.replay.risk.kill_switch);
  VT_ASSERT_TRUE(settings.replay.risk.max_daily_loss > 0.0);
  VT_ASSERT_TRUE(settings.replay.behavior.window_long_events >
                 settings.replay.behavior.window_short_events);
}

VT_TEST(Settings, OverlayMapsJsonKeysToEngineConfig) {
  const std::string text = R"({
    "instrument": "GBPUSD",
    "validation": { "max_spread": 0.0003, "stale_threshold_ms": 1234 },
    "behavior": { "window_short_events": 5 },
    "prediction": { "min_extension_z": 2.25 },
    "risk": { "kill_switch": true, "max_open_orders": 2 },
    "execution": { "ack_latency_ms": 250, "seed": 99 },
    "backtest": { "initial_capital": 5000 }
  })";
  std::string error;
  auto root = Json::parse(text, &error);
  VT_ASSERT_TRUE(root.has_value());

  Settings settings = vectra::config::default_settings();
  VT_ASSERT_TRUE(vectra::config::overlay_settings(*root, &settings, &error));

  VT_ASSERT_STREQ(settings.instrument, std::string("GBPUSD"));
  VT_ASSERT_STREQ(settings.replay.validation.expected_symbol, std::string("GBPUSD"));
  VT_ASSERT_NEAR(vectra::market::price_to_double(settings.replay.validation.max_spread),
                 0.0003, 1e-12);
  VT_ASSERT_EQ(settings.replay.validation.stale_threshold_ns, std::int64_t{1'234'000'000});
  VT_ASSERT_EQ(settings.replay.behavior.window_short_events, std::size_t{5});
  VT_ASSERT_NEAR(settings.replay.prediction.min_extension_z, 2.25, 1e-12);
  VT_ASSERT_TRUE(settings.replay.risk.kill_switch);
  VT_ASSERT_EQ(settings.replay.risk.max_open_orders, 2);
  VT_ASSERT_EQ(settings.replay.broker.ack_latency_ns, std::int64_t{250'000'000});
  VT_ASSERT_EQ(settings.replay.broker.seed, std::uint64_t{99});
  VT_ASSERT_NEAR(settings.replay.initial_capital, 5000.0, 1e-9);
}

VT_TEST(Settings, MissingConfigFileReportsError) {
  Settings settings;
  std::string error;
  VT_ASSERT_FALSE(
      vectra::config::load_settings("definitely/does/not/exist.json", &settings, &error));
  VT_ASSERT_FALSE(error.empty());
}

VT_TEST(Settings, DefaultConfigFileParses) {
  // The shipped configuration must always be valid JSON with the expected
  // instrument (skips cleanly if the working directory has no config file).
  std::ifstream file("config/default.json");
  if (!file) {
    return;
  }
  Settings settings;
  std::string error;
  VT_ASSERT_TRUE(vectra::config::load_settings("config/default.json", &settings, &error));
  VT_ASSERT_STREQ(settings.instrument, std::string("EURUSD"));
}
