// Minimal JSON parser (enough for Open-Meteo responses and theme files). Portable.
//
// It is strict, like the JSON standard: the whole text must be one value (nothing but spaces after it), numbers must be written the
// JSON way, strings may only use the standard escapes, \u escapes are checked and a surrogate pair (\ud83d\ude00) becomes one
// character. (Theme files are cleaned of comments and trailing commas first, in themes.cpp, so those stay allowed there.)
#pragma once
#include <string>
#include <cmath>
#include <vector>
#include <utility>
#include <cstdlib>
#include <cstring>

namespace mj {

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    const Value* get(const char* key) const {
        if (type != Object) return nullptr;
        for (auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    double number(double def = 0) const { return type == Number ? num : def; }
    bool isNull() const { return type == Null; }
};

class Parser {
public:
    explicit Parser(const std::string& s) : p_(s.c_str()), end_(s.c_str() + s.size()), base_(s.c_str()) {}
    bool parse(Value& out, std::string& err) {
        out = Value();
        skip();
        if (!value(out, 0)) { out = Value(); err = err_.empty() ? "parse error" : err_; return false; }
        skip();
        if (p_ != end_) { out = Value(); fail("unexpected text after the end of the data"); err = err_; return false; }
        return true;
    }
    size_t errorOffset() const { return errAt_; }              // where parsing stopped (byte offset), for line numbers in messages
private:
    const char* p_; const char* end_; std::string err_; size_t errAt_ = 0; const char* base_ = nullptr;
    void skip() { while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_; }
    bool fail(const char* m) { if (err_.empty()) { err_ = m; errAt_ = (size_t)(p_ - base_); } return false; }
    bool value(Value& v, int depth) {
        if (depth > 64) return fail("too deep");
        skip();
        if (p_ >= end_) return fail("unexpected end");
        char c = *p_;
        if (c == '{') return object(v, depth);
        if (c == '[') return array(v, depth);
        if (c == '"') { v.type = Value::String; return string(v.str); }
        if (c == 't' && end_ - p_ >= 4 && !strncmp(p_, "true", 4)) { p_ += 4; v.type = Value::Bool; v.b = true; return true; }
        if (c == 'f' && end_ - p_ >= 5 && !strncmp(p_, "false", 5)) { p_ += 5; v.type = Value::Bool; v.b = false; return true; }
        if (c == 'n' && end_ - p_ >= 4 && !strncmp(p_, "null", 4)) { p_ += 4; v.type = Value::Null; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) return number(v);
        return fail("unexpected character");
    }
    static bool digit(const char* q, const char* end) { return q < end && *q >= '0' && *q <= '9'; }
    // -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?  and finite. (strtod alone would also take "-inf", "-nan" and hex numbers.)
    bool number(Value& v) {
        const char* q = p_;
        if (*q == '-') ++q;
        if (q < end_ && *q == '0') ++q;
        else if (q < end_ && *q >= '1' && *q <= '9') { while (digit(q, end_)) ++q; }
        else return fail("bad number");
        if (q < end_ && *q == '.') { ++q; if (!digit(q, end_)) return fail("bad number"); while (digit(q, end_)) ++q; }
        if (q < end_ && (*q == 'e' || *q == 'E')) {
            ++q; if (q < end_ && (*q == '+' || *q == '-')) ++q;
            if (!digit(q, end_)) return fail("bad number");
            while (digit(q, end_)) ++q;
        }
        const std::string tok(p_, q);
        const double d = strtod(tok.c_str(), nullptr);
        if (!std::isfinite(d)) return fail("number out of range");
        v.num = d; v.type = Value::Number; p_ = q;
        return true;
    }
    static int hexDigit(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
    static bool hex4(const char* q, unsigned& cp) { cp = 0; for (int i = 0; i < 4; ++i) { const int h = hexDigit(q[i]); if (h < 0) return false; cp = cp * 16 + (unsigned)h; } return true; }
    static void putUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out.push_back((char)cp);
        else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { out.push_back((char)(0xE0 | (cp >> 12))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
        else { out.push_back((char)(0xF0 | (cp >> 18))); out.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
    }
    bool string(std::string& out) {
        ++p_; out.clear();
        while (p_ < end_ && *p_ != '"') {
            const unsigned char c = (unsigned char)*p_;
            if (c < 0x20) return fail("a raw line break or control character inside text (write \\n or \\t)");
            if (c != '\\') { out.push_back(*p_++); continue; }
            if (p_ + 1 >= end_) return fail("unterminated string");
            ++p_;
            switch (*p_) {
                case '"': out.push_back('"'); break;   case '\\': out.push_back('\\'); break;  case '/': out.push_back('/'); break;
                case 'n': out.push_back('\n'); break;  case 't': out.push_back('\t'); break;    case 'r': out.push_back('\r'); break;
                case 'b': out.push_back('\b'); break;  case 'f': out.push_back('\f'); break;
                case 'u': {
                    unsigned cp = 0;
                    if (end_ - p_ < 5 || !hex4(p_ + 1, cp)) return fail("bad \\u escape in text");
                    p_ += 4;
                    if (cp >= 0xD800 && cp < 0xDC00) {                       // first half of a pair: the second half must follow at once
                        unsigned lo = 0;
                        if (end_ - p_ >= 7 && p_[1] == '\\' && p_[2] == 'u' && hex4(p_ + 3, lo) && lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p_ += 6; }
                        else cp = 0xFFFD;                                     // a lone half: the replacement character
                    } else if (cp >= 0xDC00 && cp < 0xE000) cp = 0xFFFD;
                    putUtf8(out, cp);
                    break;
                }
                default: return fail("unknown escape in text");
            }
            ++p_;
        }
        if (p_ >= end_) return fail("unterminated string");
        ++p_;
        return true;
    }
    bool array(Value& v, int depth) {
        ++p_; v.type = Value::Array; skip();
        if (p_ < end_ && *p_ == ']') { ++p_; return true; }
        while (true) {
            v.arr.emplace_back();
            if (!value(v.arr.back(), depth + 1)) return false;
            skip();
            if (p_ < end_ && *p_ == ',') { ++p_; continue; }
            if (p_ < end_ && *p_ == ']') { ++p_; return true; }
            return fail("expected , or ]");
        }
    }
    bool object(Value& v, int depth) {
        ++p_; v.type = Value::Object; skip();
        if (p_ < end_ && *p_ == '}') { ++p_; return true; }
        while (true) {
            skip();
            if (p_ >= end_ || *p_ != '"') return fail("expected key");
            std::string key; if (!string(key)) return false;
            skip();
            if (p_ >= end_ || *p_ != ':') return fail("expected :");
            ++p_;
            v.obj.emplace_back(key, Value());
            if (!value(v.obj.back().second, depth + 1)) return false;
            skip();
            if (p_ < end_ && *p_ == ',') { ++p_; continue; }
            if (p_ < end_ && *p_ == '}') { ++p_; return true; }
            return fail("expected , or }");
        }
    }
};

inline bool parse(const std::string& s, Value& out, std::string& err) { Parser p(s); return p.parse(out, err); }
// same, and also reports the byte offset of the problem
inline bool parseAt(const std::string& s, Value& out, std::string& err, size_t& at) { Parser p(s); bool ok = p.parse(out, err); at = p.errorOffset(); return ok; }

} // namespace mj
