// json_min.h
//
// A deliberately tiny JSON parser sufficient for reading the PXB metadata
// block. It is NOT a general-purpose JSON library: it parses the subset used
// by the format (objects, arrays, strings, numbers, true/false/null) into an
// in-memory variant tree. Keeping it local avoids adding a third-party
// dependency for a handful of fields.
//
// Single-header, dependency-free, permissive (public-domain style).
//
#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdlib>
#include <cstdio>

namespace json {

struct Value;
using ValuePtr = std::shared_ptr<Value>;

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<ValuePtr> arr;
    std::map<std::string, ValuePtr> obj;

    bool is_object() const { return type == Object; }
    bool is_array() const { return type == Array; }
    bool is_string() const { return type == String; }
    bool is_number() const { return type == Number; }

    // Returns the member by value (a shared_ptr). Returning by value (rather
    // than a reference to a shared static sentinel) guarantees each call yields
    // an independent object, so callers that hold two results simultaneously
    // never alias the same null instance. A missing key returns a null
    // shared_ptr, which is falsy (operator bool) and safe to dereference-check.
    ValuePtr operator[](const std::string& k) const {
        if (type != Object) return nullptr;
        auto it = obj.find(k);
        return it == obj.end() ? nullptr : it->second;
    }
    ValuePtr operator[](size_t i) const {
        if (type != Array || i >= arr.size()) return nullptr;
        return arr[i];
    }
    std::string as_string(const std::string& def = "") const { return type == String ? str : def; }
    double as_number(double def = 0) const { return type == Number ? num : def; }
    int as_int(int def = 0) const { return type == Number ? (int)num : def; }
    bool as_bool(bool def = false) const { return type == Bool ? b : def; }
    // convenience: get member as string
    std::string get(const std::string& k, const std::string& def = "") const {
        const auto& v = (*this)[k];
        return v ? v->as_string(def) : def;
    }
};

class Parser {
public:
    static ValuePtr parse(const std::string& text, std::string* err = nullptr) {
        Parser p(text);
        p.skip_ws();
        ValuePtr v = p.parse_value();
        if (!v) { if (err) *err = p.error_; return nullptr; }
        p.skip_ws();
        if (!p.at_end()) { if (err) *err = "trailing characters"; return nullptr; }
        return v;
    }

private:
    Parser(const std::string& t) : s_(t), pos_(0) {}

    void skip_ws() {
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') pos_++;
            else break;
        }
    }
    bool at_end() const { return pos_ >= s_.size(); }
    char peek() { return pos_ < s_.size() ? s_[pos_] : '\0'; }

    ValuePtr parse_value() {
        skip_ws();
        if (at_end()) { error_ = "unexpected end"; return nullptr; }
        char c = s_[pos_];
        switch (c) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': return parse_string();
            case 't': case 'f': return parse_bool();
            case 'n': return parse_null();
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
                error_ = "unexpected character";
                return nullptr;
        }
    }

    ValuePtr parse_object() {
        auto v = std::make_shared<Value>();
        v->type = Value::Object;
        pos_++; // {
        skip_ws();
        if (peek() == '}') { pos_++; return v; }
        while (true) {
            skip_ws();
            if (peek() != '"') { error_ = "expected key string"; return nullptr; }
            ValuePtr key = parse_string();
            if (!key) return nullptr;
            skip_ws();
            if (peek() != ':') { error_ = "expected ':'"; return nullptr; }
            pos_++; // :
            ValuePtr val = parse_value();
            if (!val) return nullptr;
            v->obj[key->str] = val;
            skip_ws();
            if (peek() == ',') { pos_++; continue; }
            if (peek() == '}') { pos_++; break; }
            error_ = "expected ',' or '}'";
            return nullptr;
        }
        return v;
    }

    ValuePtr parse_array() {
        auto v = std::make_shared<Value>();
        v->type = Value::Array;
        pos_++; // [
        skip_ws();
        if (peek() == ']') { pos_++; return v; }
        while (true) {
            ValuePtr val = parse_value();
            if (!val) return nullptr;
            v->arr.push_back(val);
            skip_ws();
            if (peek() == ',') { pos_++; continue; }
            if (peek() == ']') { pos_++; break; }
            error_ = "expected ',' or ']'";
            return nullptr;
        }
        return v;
    }

    ValuePtr parse_string() {
        auto v = std::make_shared<Value>();
        v->type = Value::String;
        pos_++; // opening quote
        std::string out;
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') break;
            if (c == '\\') {
                if (pos_ >= s_.size()) { error_ = "bad escape"; return nullptr; }
                char e = s_[pos_++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        unsigned cp = 0;
                        if (pos_ + 4 > s_.size()) { error_ = "bad unicode"; return nullptr; }
                        for (int i = 0; i < 4; i++) {
                            char h = s_[pos_++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            else { error_ = "bad hex"; return nullptr; }
                        }
                        // UTF-16 surrogate pair -> non-BMP codepoint (e.g. emoji).
                        if (cp >= 0xD800 && cp <= 0xDBFF &&
                            pos_ + 6 <= s_.size() && s_[pos_] == '\\' && s_[pos_+1] == 'u') {
                            pos_ += 2;
                            unsigned lo = 0;
                            for (int i = 0; i < 4; i++) {
                                char h = s_[pos_++];
                                lo <<= 4;
                                if (h >= '0' && h <= '9') lo |= (h - '0');
                                else if (h >= 'a' && h <= 'f') lo |= (h - 'a' + 10);
                                else if (h >= 'A' && h <= 'F') lo |= (h - 'A' + 10);
                                else { error_ = "bad hex"; return nullptr; }
                            }
                            if (lo >= 0xDC00 && lo <= 0xDFFF)
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            else
                                cp = 0xFFFD;
                        } else if (cp >= 0xD800 && cp <= 0xDBFF) {
                            cp = 0xFFFD;
                        }
                        // Encode codepoint as UTF-8 (1-4 bytes).
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) {
                            out += (char)(0xC0 | (cp >> 6));
                            out += (char)(0x80 | (cp & 0x3F));
                        } else if (cp < 0x10000) {
                            out += (char)(0xE0 | (cp >> 12));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        } else {
                            out += (char)(0xF0 | (cp >> 18));
                            out += (char)(0x80 | ((cp >> 12) & 0x3F));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: out += e; break;
                }
            } else {
                out += c;
            }
        }
        v->str = out;
        return v;
    }

    ValuePtr parse_number() {
        size_t start = pos_;
        if (peek() == '-') pos_++;
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
                pos_++;
            else break;
        }
        std::string num = s_.substr(start, pos_ - start);
        auto v = std::make_shared<Value>();
        v->type = Value::Number;
        v->num = std::strtod(num.c_str(), nullptr);
        return v;
    }

    ValuePtr parse_bool() {
        auto v = std::make_shared<Value>();
        v->type = Value::Bool;
        if (s_.compare(pos_, 4, "true") == 0) { v->b = true; pos_ += 4; }
        else if (s_.compare(pos_, 5, "false") == 0) { v->b = false; pos_ += 5; }
        else { error_ = "bad literal"; return nullptr; }
        return v;
    }

    ValuePtr parse_null() {
        auto v = std::make_shared<Value>();
        v->type = Value::Null;
        if (s_.compare(pos_, 4, "null") == 0) pos_ += 4;
        else { error_ = "bad literal"; return nullptr; }
        return v;
    }

    std::string s_;
    size_t pos_;
    std::string error_;
};

} // namespace json
