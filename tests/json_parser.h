// Minimal JSON parser for correctness test case files.
// Only handles: objects, arrays of strings, arrays of arrays of strings,
// booleans, null — no numbers, no unicode escapes.

#pragma once

#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace json {

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

using Value = std::variant<std::monostate,
                           bool,
                           std::string,
                           std::vector<std::string>,
                           std::vector<std::vector<std::string>>>;

class Object {
public:
    std::optional<Value> get(const std::string& key) const {
        for (auto& [k, v] : fields_) {
            if (k == key) return v;
        }
        return std::nullopt;
    }

    std::optional<std::string> getString(const std::string& key) const {
        auto v = get(key);
        if (v && std::holds_alternative<std::string>(*v))
            return std::get<std::string>(*v);
        return std::nullopt;
    }

    std::optional<bool> getBool(const std::string& key) const {
        auto v = get(key);
        if (v && std::holds_alternative<bool>(*v))
            return std::get<bool>(*v);
        return std::nullopt;
    }

    std::optional<std::vector<std::string>> getStringArray(const std::string& key) const {
        auto v = get(key);
        if (v && std::holds_alternative<std::vector<std::string>>(*v))
            return std::get<std::vector<std::string>>(*v);
        return std::nullopt;
    }

    std::optional<std::vector<std::vector<std::string>>> getStringMatrix(const std::string& key) const {
        auto v = get(key);
        if (v && std::holds_alternative<std::vector<std::vector<std::string>>>(*v))
            return std::get<std::vector<std::vector<std::string>>>(*v);
        return std::nullopt;
    }

    void set(const std::string& key, Value v) {
        // Replace if exists
        for (auto& [k, val] : fields_) {
            if (k == key) {
                val = std::move(v);
                return;
            }
        }
        fields_.emplace_back(key, std::move(v));
    }

    const std::vector<std::pair<std::string, Value>>& fields() const { return fields_; }

private:
    std::vector<std::pair<std::string, Value>> fields_;
};

class Parser {
public:
    explicit Parser(std::string_view input) : input_(input), pos_(0) {}

    Object parseObject() {
        skipWhitespace();
        expect('{');
        Object obj;
        if (peek() != '}') {
            obj = parseMembers();
        }
        expect('}');
        return obj;
    }

    std::vector<Object> parseArray() {
        skipWhitespace();
        expect('[');
        std::vector<Object> result;
        if (peek() != ']') {
            result.push_back(parseObject());
            while (peek() == ',') {
                advance();
                result.push_back(parseObject());
            }
        }
        expect(']');
        return result;
    }

private:
    std::string_view input_;
    size_t pos_;

    char peek() {
        skipWhitespace();
        if (pos_ >= input_.size()) return '\0';
        return input_[pos_];
    }

    char advance() {
        if (pos_ >= input_.size()) throw ParseError("Unexpected end of input");
        return input_[pos_++];
    }

    void expect(char c) {
        skipWhitespace();
        if (pos_ >= input_.size() || input_[pos_] != c) {
            std::string msg = "Expected '";
            msg += c;
            msg += "' but got '";
            if (pos_ < input_.size()) {
                msg += input_[pos_];
            } else {
                msg += "<EOF>";
            }
            msg += "' at position " + std::to_string(pos_);
            throw ParseError(msg);
        }
        pos_++;
    }

    void skipWhitespace() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            pos_++;
        }
    }

    Object parseMembers() {
        Object obj;
        while (true) {
            auto key = parseString();
            skipWhitespace();
            expect(':');
            auto val = parseValue();
            obj.set(key, std::move(val));
            if (peek() == ',') {
                advance();
                if (peek() == '}') break;  // trailing comma
            } else {
                break;
            }
        }
        return obj;
    }

    Value parseValue() {
        skipWhitespace();
        char c = peek();
        if (c == '"') {
            return parseString();
        } else if (c == '[') {
            return parseArrayValue();
        } else if (c == '{') {
            // We don't support nested objects as values, but call parseObject
            // and return null for now
            advance();
            skipNestedObject();
            return std::monostate{};
        } else if (c == 't' || c == 'f') {
            return parseBool();
        } else if (c == 'n') {
            return parseNull();
        }
        throw ParseError(std::string("Unexpected character: '") + c + "'");
    }

    std::string parseString() {
        expect('"');
        std::string result;
        while (pos_ < input_.size() && input_[pos_] != '"') {
            char c = input_[pos_];
            if (c == '\\') {
                pos_++;
                if (pos_ >= input_.size()) break;
                char escaped = input_[pos_];
                switch (escaped) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    default: result += escaped; break;
                }
            } else {
                result += c;
            }
            pos_++;
        }
        expect('"');
        return result;
    }

    bool parseBool() {
        if (pos_ + 4 <= input_.size() && input_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return true;
        } else if (pos_ + 5 <= input_.size() && input_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return false;
        }
        throw ParseError("Expected true or false");
    }

    std::monostate parseNull() {
        if (pos_ + 4 <= input_.size() && input_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return std::monostate{};
        }
        throw ParseError("Expected null");
    }

    Value parseArrayValue() {
        expect('[');
        skipWhitespace();
        if (peek() == ']') {
            advance();
            return std::vector<std::string>{};
        }

        // Peek ahead to determine if this is [string, ...] or [[string], ...]
        if (peek() == '[') {
            // Array of arrays
            std::vector<std::vector<std::string>> result;
            result.push_back(parseStringArray());
            while (peek() == ',') {
                advance();
                result.push_back(parseStringArray());
            }
            expect(']');
            return result;
        } else {
            // Array of strings
            std::vector<std::string> result;
            result.push_back(parseString());
            while (peek() == ',') {
                advance();
                result.push_back(parseString());
            }
            expect(']');
            return result;
        }
    }

    std::vector<std::string> parseStringArray() {
        expect('[');
        std::vector<std::string> result;
        if (peek() != ']') {
            result.push_back(parseString());
            while (peek() == ',') {
                advance();
                result.push_back(parseString());
            }
        }
        expect(']');
        return result;
    }

    void skipNestedObject() {
        int depth = 1;
        while (pos_ < input_.size() && depth > 0) {
            char c = input_[pos_];
            if (c == '{') depth++;
            else if (c == '}') depth--;
            else if (c == '"') {
                // Skip strings
                pos_++;
                while (pos_ < input_.size() && input_[pos_] != '"') {
                    if (input_[pos_] == '\\') pos_++;
                    pos_++;
                }
            }
            pos_++;
        }
    }
};

}  // namespace json
