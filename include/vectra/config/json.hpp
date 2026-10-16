// Minimal JSON reader.
//
// The project must build with zero third-party dependencies, so configuration
// uses a small, strict JSON subset: objects, arrays, strings, numbers,
// booleans and null. Parse failures are reported with a position and never
// silently tolerated.
#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vectra::config {

class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };
  using Array = std::vector<Json>;
  using Object = std::map<std::string, Json>;

  static std::optional<Json> parse(const std::string& text, std::string* error);

  // Construction helpers used by the parser (kept out of the config API but
  // public so the file can stay dependency-free).
  static Json make_null();
  static Json make_object(Object object);
  static Json make_array(Array array);
  static Json make_string(std::string value);
  static Json make_number(double value);
  static Json make_bool(bool value);

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::Null; }
  bool is_object() const { return type_ == Type::Object; }
  bool is_array() const { return type_ == Type::Array; }
  bool is_number() const { return type_ == Type::Number; }
  bool is_bool() const { return type_ == Type::Bool; }
  bool is_string() const { return type_ == Type::String; }

  // Object access.
  const Json* find(const std::string& key) const;
  double number_or(double fallback) const;
  bool bool_or(bool fallback) const;
  std::string string_or(const std::string& fallback) const;

  // Typed getters with defaults (return the fallback when the key is absent
  // or has the wrong type).
  double get_number(const char* key, double fallback) const;
  bool get_bool(const char* key, bool fallback) const;
  std::string get_string(const char* key, const std::string& fallback) const;
  std::size_t get_size(const char* key, std::size_t fallback) const;
  const Json* get_object(const char* key) const;

  // Array access.
  std::size_t size() const;
  const Json& at(std::size_t index) const;

 private:
  Type type_ = Type::Null;
  bool bool_value_ = false;
  double number_value_ = 0.0;
  std::string string_value_;
  Array array_;
  Object object_;
};

}  // namespace vectra::config
