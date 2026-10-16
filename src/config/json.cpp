// Minimal JSON reader implementation (recursive descent, strict).
#include "vectra/config/json.hpp"

#include <cstdlib>

namespace vectra::config {

namespace {

class Parser {
 public:
  Parser(const std::string& text, std::string* error) : text_(text), error_(error) {}

  std::optional<Json> run() {
    skip_ws();
    auto value = parse_value(0);
    if (!value.has_value()) {
      return std::nullopt;
    }
    skip_ws();
    if (pos_ != text_.size()) {
      fail("trailing characters after the top-level value");
      return std::nullopt;
    }
    return value;
  }

 private:
  static constexpr int kMaxDepth = 64;

  void fail(const std::string& message) {
    if (error_ != nullptr && error_->empty()) {
      *error_ = "at offset " + std::to_string(pos_) + ": " + message;
    }
  }

  void skip_ws() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool consume(char expected) {
    if (pos_ < text_.size() && text_[pos_] == expected) {
      ++pos_;
      return true;
    }
    return false;
  }

  bool literal(const char* word) {
    const std::size_t len = std::char_traits<char>::length(word);
    if (text_.compare(pos_, len, word) == 0) {
      pos_ += len;
      return true;
    }
    return false;
  }

  std::optional<Json> parse_value(int depth) {
    if (depth > kMaxDepth) {
      fail("nesting too deep");
      return std::nullopt;
    }
    if (pos_ >= text_.size()) {
      fail("unexpected end of input");
      return std::nullopt;
    }
    const char c = text_[pos_];
    if (c == '{') {
      return parse_object(depth + 1);
    }
    if (c == '[') {
      return parse_array(depth + 1);
    }
    if (c == '"') {
      auto result = parse_string();
      if (!result.has_value()) {
        return std::nullopt;
      }
      return Json::make_string(std::move(*result));
    }
    if (c == 't' || c == 'f') {
      return parse_bool();
    }
    if (c == 'n') {
      if (literal("null")) {
        return Json::make_null();
      }
      fail("invalid literal");
      return std::nullopt;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      return parse_number();
    }
    fail(std::string("unexpected character '") + c + "'");
    return std::nullopt;
  }

  std::optional<Json> parse_object(int depth) {
    ++pos_;  // '{'
    skip_ws();
    Json::Object object;
    if (consume('}')) {
      return Json::make_object(std::move(object));
    }
    while (true) {
      skip_ws();
      if (pos_ >= text_.size() || text_[pos_] != '"') {
        fail("expected a string key");
        return std::nullopt;
      }
      auto key = parse_string();
      if (!key.has_value()) {
        return std::nullopt;
      }
      skip_ws();
      if (!consume(':')) {
        fail("expected ':' after object key");
        return std::nullopt;
      }
      skip_ws();
      auto child = parse_value(depth);
      if (!child.has_value()) {
        return std::nullopt;
      }
      object[*key] = *child;
      skip_ws();
      if (consume(',')) {
        continue;
      }
      if (consume('}')) {
        break;
      }
      fail("expected ',' or '}' in object");
      return std::nullopt;
    }
    return Json::make_object(std::move(object));
  }

  std::optional<Json> parse_array(int depth) {
    ++pos_;  // '['
    skip_ws();
    Json::Array array;
    if (consume(']')) {
      return Json::make_array(std::move(array));
    }
    while (true) {
      skip_ws();
      auto child = parse_value(depth);
      if (!child.has_value()) {
        return std::nullopt;
      }
      array.push_back(*child);
      skip_ws();
      if (consume(',')) {
        continue;
      }
      if (consume(']')) {
        break;
      }
      fail("expected ',' or ']' in array");
      return std::nullopt;
    }
    return Json::make_array(std::move(array));
  }

  // Returns the decoded string content (without quotes).
  std::optional<std::string> parse_string() {
    ++pos_;  // opening quote
    std::string out;
    while (true) {
      if (pos_ >= text_.size()) {
        fail("unterminated string");
        return std::nullopt;
      }
      const char c = text_[pos_++];
      if (c == '"') {
        break;
      }
      if (c == '\\') {
        if (pos_ >= text_.size()) {
          fail("unterminated escape");
          return std::nullopt;
        }
        const char e = text_[pos_++];
        switch (e) {
          case '"':  out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/':  out.push_back('/'); break;
          case 'b':  out.push_back('\b'); break;
          case 'f':  out.push_back('\f'); break;
          case 'n':  out.push_back('\n'); break;
          case 'r':  out.push_back('\r'); break;
          case 't':  out.push_back('\t'); break;
          case 'u': {
            if (pos_ + 4 > text_.size()) {
              fail("truncated \\u escape");
              return std::nullopt;
            }
            const std::string hex = text_.substr(pos_, 4);
            pos_ += 4;
            char* end = nullptr;
            const long code = std::strtol(hex.c_str(), &end, 16);
            if (end == nullptr || *end != '\0') {
              fail("invalid \\u escape");
              return std::nullopt;
            }
            const unsigned long cp = static_cast<unsigned long>(code);
            if (cp < 0x80) {
              out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
              out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
              out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
              out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
              out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            break;
          }
          default:
            fail("invalid escape character");
            return std::nullopt;
        }
        continue;
      }
      if (static_cast<unsigned char>(c) < 0x20) {
        fail("control character in string");
        return std::nullopt;
      }
      out.push_back(c);
    }
    return out;
  }

  std::optional<Json> parse_number() {
    const std::size_t start = pos_;
    if (pos_ < text_.size() && text_[pos_] == '-') {
      ++pos_;
    }
    while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
      ++pos_;
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      ++pos_;
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    }
    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
        ++pos_;
      }
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    }
    const std::string token = text_.substr(start, pos_ - start);
    if (token.empty() || token == "-") {
      fail("invalid number");
      return std::nullopt;
    }
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end == nullptr || *end != '\0') {
      fail("invalid number '" + token + "'");
      return std::nullopt;
    }
    return Json::make_number(value);
  }

  std::optional<Json> parse_bool() {
    if (literal("true")) {
      return Json::make_bool(true);
    }
    if (literal("false")) {
      return Json::make_bool(false);
    }
    fail("invalid literal");
    return std::nullopt;
  }

  const std::string& text_;
  std::string* error_;
  std::size_t pos_ = 0;
};

}  // namespace

Json Json::make_null() { return Json{}; }

Json Json::make_object(Object object) {
  Json j;
  j.type_ = Type::Object;
  j.object_ = std::move(object);
  return j;
}

Json Json::make_array(Array array) {
  Json j;
  j.type_ = Type::Array;
  j.array_ = std::move(array);
  return j;
}

Json Json::make_string(std::string value) {
  Json j;
  j.type_ = Type::String;
  j.string_value_ = std::move(value);
  return j;
}

Json Json::make_number(double value) {
  Json j;
  j.type_ = Type::Number;
  j.number_value_ = value;
  return j;
}

Json Json::make_bool(bool value) {
  Json j;
  j.type_ = Type::Bool;
  j.bool_value_ = value;
  return j;
}

std::optional<Json> Json::parse(const std::string& text, std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  Parser parser(text, error);
  return parser.run();
}

const Json* Json::find(const std::string& key) const {
  if (type_ != Type::Object) {
    return nullptr;
  }
  const auto it = object_.find(key);
  return it == object_.end() ? nullptr : &it->second;
}

double Json::number_or(double fallback) const {
  return type_ == Type::Number ? number_value_ : fallback;
}

bool Json::bool_or(bool fallback) const { return type_ == Type::Bool ? bool_value_ : fallback; }

std::string Json::string_or(const std::string& fallback) const {
  return type_ == Type::String ? string_value_ : fallback;
}

double Json::get_number(const char* key, double fallback) const {
  const Json* v = find(key);
  return v != nullptr ? v->number_or(fallback) : fallback;
}

bool Json::get_bool(const char* key, bool fallback) const {
  const Json* v = find(key);
  return v != nullptr ? v->bool_or(fallback) : fallback;
}

std::string Json::get_string(const char* key, const std::string& fallback) const {
  const Json* v = find(key);
  return v != nullptr ? v->string_or(fallback) : fallback;
}

std::size_t Json::get_size(const char* key, std::size_t fallback) const {
  const Json* v = find(key);
  if (v == nullptr || !v->is_number() || v->number_value_ < 0.0) {
    return fallback;
  }
  return static_cast<std::size_t>(v->number_value_);
}

const Json* Json::get_object(const char* key) const {
  const Json* v = find(key);
  return (v != nullptr && v->is_object()) ? v : nullptr;
}

std::size_t Json::size() const {
  if (type_ == Type::Array) {
    return array_.size();
  }
  if (type_ == Type::Object) {
    return object_.size();
  }
  return 0;
}

const Json& Json::at(std::size_t index) const {
  static const Json null_value;
  if (type_ != Type::Array || index >= array_.size()) {
    return null_value;
  }
  return array_[index];
}

}  // namespace vectra::config
