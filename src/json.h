#pragma once
// Minimal header-only JSON for LSP/DAP — no external dependencies.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace zscript {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    // ── construction ────────────────────────────────────────────────────────
    Json()                        : type_(Type::Null) {}
    Json(std::nullptr_t)          : type_(Type::Null) {}
    Json(bool b)                  : type_(Type::Bool),   b_(b) {}
    Json(int v)                   : type_(Type::Number), n_((double)v) {}
    Json(int64_t v)               : type_(Type::Number), n_((double)v) {}
    Json(double v)                : type_(Type::Number), n_(v) {}
    Json(const char* s)           : type_(Type::String), s_(s) {}
    Json(std::string s)           : type_(Type::String), s_(std::move(s)) {}
    Json(std::vector<Json> a)     : type_(Type::Array),
        a_(std::make_shared<std::vector<Json>>(std::move(a))) {}
    Json(std::map<std::string,Json> o) : type_(Type::Object),
        o_(std::make_shared<std::map<std::string,Json>>(std::move(o))) {}

    static Json array()  { return Json(std::vector<Json>{}); }
    static Json object() { return Json(std::map<std::string,Json>{}); }

    // ── type checks ─────────────────────────────────────────────────────────
    bool is_null()   const { return type_ == Type::Null;   }
    bool is_bool()   const { return type_ == Type::Bool;   }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array()  const { return type_ == Type::Array;  }
    bool is_object() const { return type_ == Type::Object; }

    // ── accessors ────────────────────────────────────────────────────────────
    bool               as_bool()   const { return b_; }
    double             as_double() const { return n_; }
    int64_t            as_int()    const { return (int64_t)n_; }
    const std::string& as_string() const { return s_; }

    std::vector<Json>&       as_array()       { return *a_; }
    const std::vector<Json>& as_array()  const { return *a_; }
    std::map<std::string,Json>&       as_object()       { return *o_; }
    const std::map<std::string,Json>& as_object() const { return *o_; }

    size_t size() const {
        if (is_array())  return a_->size();
        if (is_object()) return o_->size();
        return 0;
    }
    bool empty() const { return size() == 0; }

    void push_back(Json v) {
        if (type_ == Type::Null) { type_ = Type::Array; a_ = std::make_shared<std::vector<Json>>(); }
        a_->push_back(std::move(v));
    }

    // Object subscript — auto-vivify
    Json& operator[](const std::string& key) {
        if (type_ == Type::Null) { type_ = Type::Object; o_ = std::make_shared<std::map<std::string,Json>>(); }
        return (*o_)[key];
    }
    const Json& operator[](const std::string& key) const {
        static Json nil;
        if (!o_) return nil;
        auto it = o_->find(key);
        return it != o_->end() ? it->second : nil;
    }
    Json& operator[](size_t i)       { return (*a_)[i]; }
    const Json& operator[](size_t i) const { return (*a_)[i]; }

    bool contains(const std::string& key) const {
        return o_ && o_->find(key) != o_->end();
    }

    // value_or helpers
    std::string string_or(const std::string& def) const {
        return is_string() ? s_ : def;
    }
    int64_t int_or(int64_t def) const {
        return is_number() ? (int64_t)n_ : def;
    }
    bool bool_or(bool def) const {
        return is_bool() ? b_ : def;
    }

    // ── serialization ────────────────────────────────────────────────────────
    std::string dump(int indent = -1, int depth = 0) const {
        std::string s;
        dump_into(s, indent, depth);
        return s;
    }

    // ── parsing ──────────────────────────────────────────────────────────────
    static Json parse(const std::string& s) { return parse(s.data(), s.size()); }
    static Json parse(const char* data, size_t len) {
        size_t pos = 0;
        Json v = parse_value(data, len, pos);
        return v;
    }

private:
    Type   type_ = Type::Null;
    bool   b_    = false;
    double n_    = 0;
    std::string  s_;
    std::shared_ptr<std::vector<Json>>            a_;
    std::shared_ptr<std::map<std::string,Json>>   o_;

    // ── serializer ───────────────────────────────────────────────────────────
    static void escape_string(std::string& out, const std::string& s) {
        out += '"';
        for (unsigned char c : s) {
            if      (c == '"')  { out += "\\\""; }
            else if (c == '\\') { out += "\\\\"; }
            else if (c == '\n') { out += "\\n"; }
            else if (c == '\r') { out += "\\r"; }
            else if (c == '\t') { out += "\\t"; }
            else if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else { out += (char)c; }
        }
        out += '"';
    }

    void dump_into(std::string& out, int indent, int depth) const {
        auto nl = [&]() {
            if (indent >= 0) { out += '\n'; out.append(indent * (depth+1), ' '); }
        };
        auto nl_close = [&]() {
            if (indent >= 0) { out += '\n'; out.append(indent * depth, ' '); }
        };
        switch (type_) {
            case Type::Null:   out += "null"; break;
            case Type::Bool:   out += b_ ? "true" : "false"; break;
            case Type::Number: {
                char buf[64];
                if (n_ == (int64_t)n_) snprintf(buf, sizeof(buf), "%lld", (long long)(int64_t)n_);
                else                   snprintf(buf, sizeof(buf), "%.17g", n_);
                out += buf;
                break;
            }
            case Type::String: escape_string(out, s_); break;
            case Type::Array: {
                out += '[';
                bool first = true;
                for (auto& v : *a_) {
                    if (!first) out += ',';
                    nl();
                    v.dump_into(out, indent, depth + 1);
                    first = false;
                }
                if (!a_->empty()) nl_close();
                out += ']';
                break;
            }
            case Type::Object: {
                out += '{';
                bool first = true;
                for (auto& [k, v] : *o_) {
                    if (!first) out += ',';
                    nl();
                    escape_string(out, k);
                    out += ':';
                    if (indent >= 0) out += ' ';
                    v.dump_into(out, indent, depth + 1);
                    first = false;
                }
                if (!o_->empty()) nl_close();
                out += '}';
                break;
            }
        }
    }

    // ── parser ───────────────────────────────────────────────────────────────
    static void skip_ws(const char* d, size_t len, size_t& pos) {
        while (pos < len && (d[pos]==' '||d[pos]=='\t'||d[pos]=='\n'||d[pos]=='\r')) ++pos;
    }

    static std::string parse_string(const char* d, size_t len, size_t& pos) {
        assert(d[pos] == '"'); ++pos;
        std::string s;
        while (pos < len && d[pos] != '"') {
            if (d[pos] == '\\') {
                ++pos;
                if (pos >= len) break;
                switch (d[pos]) {
                    case '"':  s += '"';  break;
                    case '\\': s += '\\'; break;
                    case '/':  s += '/';  break;
                    case 'n':  s += '\n'; break;
                    case 'r':  s += '\r'; break;
                    case 't':  s += '\t'; break;
                    case 'b':  s += '\b'; break;
                    case 'f':  s += '\f'; break;
                    case 'u': {
                        // 4-hex unicode — emit as-is in UTF-8 for ASCII range
                        char hex[5] = {};
                        for (int i = 0; i < 4 && pos+1 < len; ++i) hex[i] = d[++pos];
                        unsigned cp = (unsigned)strtoul(hex, nullptr, 16);
                        if (cp < 0x80) s += (char)cp;
                        else if (cp < 0x800) {
                            s += (char)(0xC0 | (cp>>6));
                            s += (char)(0x80 | (cp&0x3F));
                        } else {
                            s += (char)(0xE0 | (cp>>12));
                            s += (char)(0x80 | ((cp>>6)&0x3F));
                            s += (char)(0x80 | (cp&0x3F));
                        }
                        break;
                    }
                    default: s += d[pos]; break;
                }
            } else {
                s += d[pos];
            }
            ++pos;
        }
        if (pos < len) ++pos; // closing "
        return s;
    }

    static Json parse_value(const char* d, size_t len, size_t& pos) {
        skip_ws(d, len, pos);
        if (pos >= len) return Json{};

        char c = d[pos];

        if (c == '"') return Json(parse_string(d, len, pos));

        if (c == '{') {
            ++pos;
            Json obj = Json::object();
            skip_ws(d, len, pos);
            if (pos < len && d[pos] == '}') { ++pos; return obj; }
            while (pos < len) {
                skip_ws(d, len, pos);
                std::string key = parse_string(d, len, pos);
                skip_ws(d, len, pos);
                if (pos < len && d[pos] == ':') ++pos;
                Json val = parse_value(d, len, pos);
                obj[key] = std::move(val);
                skip_ws(d, len, pos);
                if (pos < len && d[pos] == ',') { ++pos; continue; }
                if (pos < len && d[pos] == '}') { ++pos; break; }
                break;
            }
            return obj;
        }

        if (c == '[') {
            ++pos;
            Json arr = Json::array();
            skip_ws(d, len, pos);
            if (pos < len && d[pos] == ']') { ++pos; return arr; }
            while (pos < len) {
                Json val = parse_value(d, len, pos);
                arr.push_back(std::move(val));
                skip_ws(d, len, pos);
                if (pos < len && d[pos] == ',') { ++pos; continue; }
                if (pos < len && d[pos] == ']') { ++pos; break; }
                break;
            }
            return arr;
        }

        if (c == 't' && pos+3 < len && d[pos+1]=='r' && d[pos+2]=='u' && d[pos+3]=='e')
            { pos += 4; return Json(true); }
        if (c == 'f' && pos+4 < len && d[pos+1]=='a' && d[pos+2]=='l' && d[pos+3]=='s' && d[pos+4]=='e')
            { pos += 5; return Json(false); }
        if (c == 'n' && pos+3 < len && d[pos+1]=='u' && d[pos+2]=='l' && d[pos+3]=='l')
            { pos += 4; return Json(nullptr); }

        // number
        if (c == '-' || (c >= '0' && c <= '9')) {
            size_t start = pos;
            if (d[pos] == '-') ++pos;
            while (pos < len && d[pos] >= '0' && d[pos] <= '9') ++pos;
            bool is_float = false;
            if (pos < len && d[pos] == '.') { is_float = true; ++pos; while (pos < len && d[pos] >= '0' && d[pos] <= '9') ++pos; }
            if (pos < len && (d[pos] == 'e' || d[pos] == 'E')) {
                is_float = true; ++pos;
                if (pos < len && (d[pos]=='+' || d[pos]=='-')) ++pos;
                while (pos < len && d[pos] >= '0' && d[pos] <= '9') ++pos;
            }
            std::string num_str(d + start, pos - start);
            if (is_float) return Json(std::stod(num_str));
            return Json((int64_t)std::stoll(num_str));
        }

        return Json{};
    }
};

} // namespace zscript
