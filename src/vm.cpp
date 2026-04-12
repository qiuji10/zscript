#include "vm.h"
#include "compiler.h"
#include "hotpatch.h"
#include "lexer.h"
#include "parser.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

namespace zscript {

// ===========================================================================
// Constructor / Destructor
// ===========================================================================
VM::VM() {
    frames_.reserve(MAX_FRAMES);
}

// Destructor must be defined here so ~HotpatchManager is visible.
VM::~VM() = default;

// ===========================================================================
// Setup
// ===========================================================================
void VM::register_function(const std::string& name, NativeFunction::Fn fn) {
    globals_[name] = Value::from_native(name, std::move(fn));
}

void VM::set_global(const std::string& name, Value v) {
    globals_[name] = std::move(v);
}

Value VM::get_global(const std::string& name) const {
    auto it = globals_.find(name);
    return (it != globals_.end()) ? it->second : Value::nil();
}

// ===========================================================================
// GC
// ===========================================================================
void VM::mark_roots(GC& gc) {
    // Mark all live registers
    for (auto& v : regs_) gc.mark_value(v);
    // Mark all globals
    for (auto& [k, v] : globals_) gc.mark_value(v);
    // Mark module exports
    for (auto& [name, mod] : loader_.modules()) {
        for (auto& [k, v] : mod->exports) gc.mark_value(v);
        gc.mark_value(mod->on_reload_fn);
    }
}

void VM::gc_collect() {
    gc_.collect([this](GC& gc) { mark_roots(gc); });
}

// ===========================================================================
// Module system
// ===========================================================================
bool VM::import_module(const std::string& name) {
    std::string err;
    Module* mod = loader_.load(name, *this, err);
    if (!mod) {
        last_error_ = {err, ""};
        return false;
    }
    // Merge module exports into VM globals
    for (auto& [k, v] : mod->exports) {
        globals_[k] = v;
    }
    return true;
}

bool VM::execute_module(Module& mod, std::string& error_msg) {
    if (!mod.chunk || !mod.chunk->main_proto) {
        error_msg = "module '" + mod.name + "' has no compiled code";
        return false;
    }

    // Save current globals, run module with a fresh namespace, then collect exports.
    auto saved_globals = globals_;
    globals_.clear();
    // Give the module access to stdlib (already registered in saved_globals)
    for (auto& [k, v] : saved_globals) globals_[k] = v;

    frames_.clear();
    frames_.push_back({mod.chunk->main_proto, 0, 0});

    bool ok = false;
    try {
        ok = run();
    } catch (const RuntimeError& e) {
        last_error_ = e;
        error_msg   = e.message;
    }

    // Collect exports — everything the module defined that wasn't in stdlib
    for (auto& [k, v] : globals_) {
        if (saved_globals.find(k) == saved_globals.end()) {
            mod.exports[k] = v;
        }
    }

    // Restore caller's globals
    globals_ = std::move(saved_globals);

    return ok;
}

// ===========================================================================
// invoke_from_native — re-entrant closure call usable from native callbacks
// ===========================================================================
std::vector<Value> VM::invoke_from_native(Value callee, std::vector<Value> args) {
    if (callee.is_native()) {
        return callee.as_native()->fn(std::move(args));
    }
    if (!callee.is_closure()) return {Value::nil()};

    // Find the register high-water mark across all active frames.
    uint16_t top = 0;
    for (auto& f : frames_) {
        uint16_t frame_top = (uint16_t)(f.base_reg + f.proto->max_regs);
        if (frame_top > top) top = frame_top;
    }
    top += 4; // safety gap
    if (top + (uint16_t)args.size() + 4 >= MAX_REGS) {
        throw RuntimeError{"invoke_from_native: register overflow", ""};
    }

    Proto* proto = callee.as_closure()->proto;
    regs_[top] = callee;
    for (size_t i = 0; i < args.size(); ++i)
        regs_[top + 1 + i] = args[i];
    for (size_t i = args.size(); i < proto->num_params; ++i)
        regs_[top + 1 + i] = Value::nil();

    size_t saved_depth = frames_.size();
    CallFrame frame;
    frame.proto       = proto;
    frame.pc          = 0;
    frame.base_reg    = (uint8_t)(top + 1);
    frame.num_results = 1;
    frame.closure     = callee.as_closure();
    frames_.push_back(frame);

    if (!run(saved_depth)) {
        throw RuntimeError{last_error_.message, last_error_.trace};
    }
    return {regs_[top]};
}

// ===========================================================================
// Standard library
// ===========================================================================
bool VM::load_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) { last_error_ = {"cannot open file: " + path, ""}; return false; }
    std::ostringstream ss; ss << f.rdbuf();

    // Add the script's directory to the module search path so that
    // `import utils` resolves to sibling files.
    auto last_sep = path.find_last_of("/\\");
    if (last_sep != std::string::npos)
        loader_.add_search_path(path.substr(0, last_sep));
    else
        loader_.add_search_path(".");

    Lexer lexer(ss.str(), path);
    auto tokens = lexer.tokenize();
    if (lexer.has_errors()) {
        last_error_ = {lexer.errors()[0].message, ""}; return false;
    }
    Parser parser(std::move(tokens), path);
    Program prog = parser.parse();
    if (parser.has_errors()) {
        last_error_ = {parser.errors()[0].message, ""}; return false;
    }
    Compiler compiler(engine_);
    auto chunk = compiler.compile(prog, path);
    if (compiler.has_errors()) {
        last_error_ = {compiler.errors()[0].message, ""}; return false;
    }
    return execute(*chunk);
}

void VM::open_stdlib() {
    // ── helpers ────────────────────────────────────────────────────────────
    auto num_arg = [](const std::vector<Value>& a, size_t i) -> double {
        if (i >= a.size()) return 0.0;
        return a[i].to_float();
    };
    auto str_arg = [](const std::vector<Value>& a, size_t i) -> std::string {
        if (i >= a.size()) return "";
        return a[i].to_string();
    };

    // ── print / log ────────────────────────────────────────────────────────
    register_function("print", [](std::vector<Value> args) -> std::vector<Value> {
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) std::cout << '\t';
            std::cout << args[i].to_string();
        }
        std::cout << '\n';
        return {};
    });
    register_function("log", [](std::vector<Value> args) -> std::vector<Value> {
        for (auto& a : args) std::cout << a.to_string();
        std::cout << '\n';
        return {};
    });

    // ── type conversion ────────────────────────────────────────────────────
    register_function("tostring", [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {Value::from_string("nil")};
        return {Value::from_string(args[0].to_string())};
    });
    register_function("tonumber", [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {Value::nil()};
        auto& a = args[0];
        if (a.is_int() || a.is_float()) return {a};
        if (a.is_string()) {
            try {
                size_t pos;
                int64_t iv = std::stoll(a.as_string(), &pos);
                if (pos == a.as_string().size()) return {Value::from_int(iv)};
                double  dv = std::stod(a.as_string(), &pos);
                if (pos == a.as_string().size()) return {Value::from_float(dv)};
            } catch (...) {}
        }
        return {Value::nil()};
    });
    register_function("tobool", [](std::vector<Value> args) -> std::vector<Value> {
        return {Value::from_bool(!args.empty() && args[0].truthy())};
    });
    register_function("type", [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {Value::from_string("nil")};
        return {Value::from_string(args[0].type_name())};
    });

    // ── assert / error ─────────────────────────────────────────────────────
    register_function("assert", [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty() || !args[0].truthy()) {
            std::string msg = (args.size() > 1) ? args[1].to_string() : "assertion failed";
            throw RuntimeError{msg, ""};
        }
        return {args[0]};
    });
    register_function("error", [](std::vector<Value> args) -> std::vector<Value> {
        std::string msg = args.empty() ? "error" : args[0].to_string();
        throw RuntimeError{msg, ""};
        return {};
    });

    // ── global math shortcuts ──────────────────────────────────────────────
    register_function("max", [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {Value::nil()};
        Value best = args[0];
        for (size_t i = 1; i < args.size(); ++i) {
            auto& v = args[i];
            bool gt = (best.is_int() && v.is_int()) ? (v.as_int() > best.as_int())
                                                     : (v.to_float() > best.to_float());
            if (gt) best = v;
        }
        return {best};
    });
    register_function("min", [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {Value::nil()};
        Value best = args[0];
        for (size_t i = 1; i < args.size(); ++i) {
            auto& v = args[i];
            bool lt = (best.is_int() && v.is_int()) ? (v.as_int() < best.as_int())
                                                     : (v.to_float() < best.to_float());
            if (lt) best = v;
        }
        return {best};
    });

    // ── len (global, works on string and table) ────────────────────────────
    register_function("len", [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {Value::from_int(0)};
        auto& v = args[0];
        if (v.is_string()) return {Value::from_int((int64_t)v.as_string().size())};
        if (v.is_table())  return {Value::from_int((int64_t)v.as_table()->count())};
        return {Value::from_int(0)};
    });

    // ── range(n) / range(start, stop) / range(start, stop, step) ──────────
    register_function("range", [](std::vector<Value> args) -> std::vector<Value> {
        int64_t start = 0, stop = 0, step = 1;
        if (args.size() == 1) {
            stop = args[0].is_int() ? args[0].as_int() : (int64_t)args[0].to_float();
        } else if (args.size() >= 2) {
            start = args[0].is_int() ? args[0].as_int() : (int64_t)args[0].to_float();
            stop  = args[1].is_int() ? args[1].as_int() : (int64_t)args[1].to_float();
            if (args.size() >= 3)
                step = args[2].is_int() ? args[2].as_int() : (int64_t)args[2].to_float();
        }
        if (step == 0) throw RuntimeError{"range: step cannot be 0", ""};
        Value tbl = Value::from_table();
        auto* t = tbl.as_table();
        int64_t idx = 0;
        for (int64_t i = start; (step > 0 ? i < stop : i > stop); i += step)
            t->set_index(idx++, Value::from_int(i));
        return {tbl};
    });

    // ── type casts ─────────────────────────────────────────────────────────
    register_function("int", [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {Value::from_int(0)};
        auto& a = args[0];
        if (a.is_int())   return {a};
        if (a.is_float()) return {Value::from_int((int64_t)a.as_float())};
        if (a.is_string()) {
            try {
                size_t pos;
                int64_t iv = std::stoll(a.as_string(), &pos);
                if (pos == a.as_string().size()) return {Value::from_int(iv)};
                double  dv = std::stod(a.as_string(), &pos);
                if (pos == a.as_string().size()) return {Value::from_int((int64_t)dv)};
            } catch (...) {}
        }
        return {Value::nil()};
    });
    register_function("float", [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {Value::from_float(0.0)};
        auto& a = args[0];
        if (a.is_float()) return {a};
        if (a.is_int())   return {Value::from_float((double)a.as_int())};
        if (a.is_string()) {
            try {
                size_t pos;
                double dv = std::stod(a.as_string(), &pos);
                if (pos == a.as_string().size()) return {Value::from_float(dv)};
            } catch (...) {}
        }
        return {Value::nil()};
    });
    register_function("str", [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {Value::from_string("nil")};
        return {Value::from_string(args[0].to_string())};
    });

    // ── math table ─────────────────────────────────────────────────────────
    Value math_tbl = Value::from_table();
    auto* mt = math_tbl.as_table();
    auto mfn = [&](const std::string& key, NativeFunction::Fn fn) {
        mt->set(key, Value::from_native("math." + key, std::move(fn)));
    };

    // Constants
    mt->set("pi",   Value::from_float(M_PI));
    mt->set("huge", Value::from_float(std::numeric_limits<double>::infinity()));
    mt->set("inf",  Value::from_float(std::numeric_limits<double>::infinity()));
    mt->set("nan",  Value::from_float(std::numeric_limits<double>::quiet_NaN()));

    // Basic
    mfn("floor", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        return {Value::from_int((int64_t)std::floor(num_arg(a, 0)))};
    });
    mfn("ceil", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        return {Value::from_int((int64_t)std::ceil(num_arg(a, 0)))};
    });
    mfn("round", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        return {Value::from_int((int64_t)std::round(num_arg(a, 0)))};
    });
    mfn("abs", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty()) return {Value::from_int(0)};
        if (a[0].is_int()) return {Value::from_int(std::abs(a[0].as_int()))};
        return {Value::from_float(std::abs(a[0].to_float()))};
    });
    mfn("sqrt", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        return {Value::from_float(std::sqrt(num_arg(a, 0)))};
    });
    mfn("pow", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        return {Value::from_float(std::pow(num_arg(a, 0), num_arg(a, 1)))};
    });
    mfn("exp", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        return {Value::from_float(std::exp(num_arg(a, 0)))};
    });
    mfn("log", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        double x = num_arg(a, 0);
        if (a.size() >= 2) return {Value::from_float(std::log(x) / std::log(num_arg(a, 1)))};
        return {Value::from_float(std::log(x))};
    });
    mfn("log10", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        return {Value::from_float(std::log10(num_arg(a, 0)))};
    });

    // Trig
    mfn("sin",  [&num_arg](std::vector<Value> a) -> std::vector<Value> { return {Value::from_float(std::sin(num_arg(a,0)))}; });
    mfn("cos",  [&num_arg](std::vector<Value> a) -> std::vector<Value> { return {Value::from_float(std::cos(num_arg(a,0)))}; });
    mfn("tan",  [&num_arg](std::vector<Value> a) -> std::vector<Value> { return {Value::from_float(std::tan(num_arg(a,0)))}; });
    mfn("asin", [&num_arg](std::vector<Value> a) -> std::vector<Value> { return {Value::from_float(std::asin(num_arg(a,0)))}; });
    mfn("acos", [&num_arg](std::vector<Value> a) -> std::vector<Value> { return {Value::from_float(std::acos(num_arg(a,0)))}; });
    mfn("atan", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        if (a.size() >= 2) return {Value::from_float(std::atan2(num_arg(a,0), num_arg(a,1)))};
        return {Value::from_float(std::atan(num_arg(a,0)))};
    });

    // Clamp / sign / lerp
    mfn("clamp", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        double v = num_arg(a,0), lo = num_arg(a,1), hi = num_arg(a,2);
        return {Value::from_float(v < lo ? lo : v > hi ? hi : v)};
    });
    mfn("sign", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        double v = num_arg(a,0);
        return {Value::from_int(v > 0 ? 1 : v < 0 ? -1 : 0)};
    });
    mfn("lerp", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        double lo = num_arg(a,0), hi = num_arg(a,1), t = num_arg(a,2);
        return {Value::from_float(lo + t * (hi - lo))};
    });

    // Min / max inside math table (variadic)
    mfn("max", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty()) return {Value::nil()};
        double best = a[0].to_float();
        for (size_t i = 1; i < a.size(); ++i) best = std::max(best, a[i].to_float());
        return {Value::from_float(best)};
    });
    mfn("min", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty()) return {Value::nil()};
        double best = a[0].to_float();
        for (size_t i = 1; i < a.size(); ++i) best = std::min(best, a[i].to_float());
        return {Value::from_float(best)};
    });

    mfn("rad", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        return {Value::from_float(num_arg(a,0) * M_PI / 180.0)};
    });
    mfn("deg", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        return {Value::from_float(num_arg(a,0) * 180.0 / M_PI)};
    });
    mfn("fmod", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        return {Value::from_float(std::fmod(num_arg(a,0), num_arg(a,1)))};
    });
    mfn("is_nan", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        return {Value::from_bool(std::isnan(num_arg(a,0)))};
    });
    mfn("is_inf", [&num_arg](std::vector<Value> a) -> std::vector<Value> {
        return {Value::from_bool(std::isinf(num_arg(a,0)))};
    });
    mfn("div", [](std::vector<Value> a) -> std::vector<Value> {
        // Integer division: math.div(10, 3) == 3
        if (a.size() < 2) return {Value::from_int(0)};
        int64_t num = a[0].is_int() ? a[0].as_int() : (int64_t)a[0].to_float();
        int64_t den = a[1].is_int() ? a[1].as_int() : (int64_t)a[1].to_float();
        if (den == 0) throw RuntimeError{"math.div: division by zero", ""};
        return {Value::from_int(num / den)};
    });

    globals_["math"] = math_tbl;

    // ── string table ───────────────────────────────────────────────────────
    Value str_tbl = Value::from_table();
    auto* st = str_tbl.as_table();
    auto sfn = [&](const std::string& key, NativeFunction::Fn fn) {
        st->set(key, Value::from_native("string." + key, std::move(fn)));
    };

    sfn("len", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        return {Value::from_int((int64_t)str_arg(a,0).size())};
    });
    sfn("sub", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        // sub(s, start, end?) — 0-based, end exclusive
        std::string s = str_arg(a, 0);
        int64_t start = a.size() > 1 ? (a[1].is_int() ? a[1].as_int() : (int64_t)a[1].to_float()) : 0;
        int64_t len   = (int64_t)s.size();
        if (start < 0) start = std::max<int64_t>(0, len + start);
        if (start >= len) return {Value::from_string("")};
        if (a.size() > 2) {
            int64_t end = a[2].is_int() ? a[2].as_int() : (int64_t)a[2].to_float();
            if (end < 0) end = len + end;
            end = std::min(end, len);
            return {Value::from_string(s.substr((size_t)start, (size_t)std::max<int64_t>(0, end - start)))};
        }
        return {Value::from_string(s.substr((size_t)start))};
    });
    sfn("upper", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::string s = str_arg(a, 0);
        for (auto& c : s) c = (char)std::toupper((unsigned char)c);
        return {Value::from_string(s)};
    });
    sfn("lower", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::string s = str_arg(a, 0);
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        return {Value::from_string(s)};
    });
    sfn("trim", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::string s = str_arg(a, 0);
        size_t l = s.find_first_not_of(" \t\r\n");
        size_t r = s.find_last_not_of(" \t\r\n");
        if (l == std::string::npos) return {Value::from_string("")};
        return {Value::from_string(s.substr(l, r - l + 1))};
    });
    sfn("trim_start", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::string s = str_arg(a, 0);
        size_t l = s.find_first_not_of(" \t\r\n");
        return {Value::from_string(l == std::string::npos ? "" : s.substr(l))};
    });
    sfn("trim_end", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::string s = str_arg(a, 0);
        size_t r = s.find_last_not_of(" \t\r\n");
        return {Value::from_string(r == std::string::npos ? "" : s.substr(0, r + 1))};
    });
    sfn("contains", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        return {Value::from_bool(str_arg(a,0).find(str_arg(a,1)) != std::string::npos)};
    });
    sfn("starts_with", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::string s = str_arg(a,0), p = str_arg(a,1);
        return {Value::from_bool(s.size() >= p.size() && s.substr(0, p.size()) == p)};
    });
    sfn("ends_with", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::string s = str_arg(a,0), p = str_arg(a,1);
        return {Value::from_bool(s.size() >= p.size() && s.substr(s.size()-p.size()) == p)};
    });
    sfn("find", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::string s = str_arg(a,0), p = str_arg(a,1);
        size_t pos = s.find(p);
        if (pos == std::string::npos) return {Value::from_int(-1)};
        return {Value::from_int((int64_t)pos)};
    });
    sfn("replace", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::string s = str_arg(a,0), from = str_arg(a,1), to = str_arg(a,2);
        if (from.empty()) return {Value::from_string(s)};
        std::string out;
        size_t pos = 0, found;
        while ((found = s.find(from, pos)) != std::string::npos) {
            out += s.substr(pos, found - pos);
            out += to;
            pos = found + from.size();
        }
        out += s.substr(pos);
        return {Value::from_string(out)};
    });
    sfn("split", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::string s = str_arg(a,0), delim = str_arg(a,1);
        Value tbl = Value::from_table();
        auto* t = tbl.as_table();
        int64_t idx = 0;
        if (delim.empty()) {
            for (char c : s) t->set_index(idx++, Value::from_string(std::string(1,c)));
        } else {
            size_t pos = 0, found;
            while ((found = s.find(delim, pos)) != std::string::npos) {
                t->set_index(idx++, Value::from_string(s.substr(pos, found - pos)));
                pos = found + delim.size();
            }
            t->set_index(idx, Value::from_string(s.substr(pos)));
        }
        return {tbl};
    });
    sfn("join", [](std::vector<Value> a) -> std::vector<Value> {
        // join(array, sep)
        if (a.empty() || !a[0].is_table()) return {Value::from_string("")};
        std::string sep = a.size() > 1 ? a[1].to_string() : "";
        auto* t = a[0].as_table();
        std::string out;
        for (size_t i = 0; i < t->array.size(); ++i) {
            if (i) out += sep;
            out += t->array[i].to_string();
        }
        return {Value::from_string(out)};
    });
    sfn("rep", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::string s = str_arg(a,0);
        int64_t n = a.size() > 1 ? (a[1].is_int() ? a[1].as_int() : (int64_t)a[1].to_float()) : 0;
        std::string out;
        for (int64_t i = 0; i < n; ++i) out += s;
        return {Value::from_string(out)};
    });
    sfn("byte", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::string s = str_arg(a,0);
        int64_t i = a.size() > 1 ? (a[1].is_int() ? a[1].as_int() : (int64_t)a[1].to_float()) : 0;
        if (s.empty() || i < 0 || i >= (int64_t)s.size()) return {Value::nil()};
        return {Value::from_int((unsigned char)s[(size_t)i])};
    });
    sfn("char", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty()) return {Value::from_string("")};
        int64_t code = a[0].is_int() ? a[0].as_int() : (int64_t)a[0].to_float();
        return {Value::from_string(std::string(1, (char)(code & 0xFF)))};
    });
    sfn("format", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        // printf-style formatting: supports flags, width, precision + d/i/f/g/e/s/%
        std::string fmt = str_arg(a, 0);
        std::string out;
        size_t arg_idx = 1;
        for (size_t i = 0; i < fmt.size(); ++i) {
            if (fmt[i] != '%') { out += fmt[i]; continue; }
            ++i;
            if (i >= fmt.size()) { out += '%'; break; }
            if (fmt[i] == '%') { out += '%'; continue; }

            // Collect the full format specifier: [-+ #0]*[0-9]*[.0-9]*[diouxXeEfgGs]
            std::string spec = "%";
            while (i < fmt.size() && (fmt[i] == '-' || fmt[i] == '+' ||
                   fmt[i] == ' ' || fmt[i] == '#' || fmt[i] == '0')) {
                spec += fmt[i++];
            }
            while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') spec += fmt[i++];
            if (i < fmt.size() && fmt[i] == '.') {
                spec += fmt[i++];
                while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') spec += fmt[i++];
            }
            if (i >= fmt.size()) { out += spec; break; }
            char conv = fmt[i];
            spec += conv;

            if (arg_idx >= a.size()) { out += spec; continue; }
            auto& v = a[arg_idx++];
            char buf[128];
            if (conv == 'd' || conv == 'i') {
                // replace conv with lld
                spec.back() = 'd';
                spec.insert(spec.size() - 1, "ll");
                snprintf(buf, sizeof(buf), spec.c_str(), (long long)v.as_int());
            } else if (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' ||
                       conv == 'g' || conv == 'G') {
                snprintf(buf, sizeof(buf), spec.c_str(), v.to_float());
            } else if (conv == 's') {
                out += v.to_string(); continue;
            } else if (conv == 'x' || conv == 'X' || conv == 'o' || conv == 'u') {
                spec.back() = conv;
                spec.insert(spec.size() - 1, "ll");
                snprintf(buf, sizeof(buf), spec.c_str(), (unsigned long long)v.as_int());
            } else {
                out += spec; continue;
            }
            out += buf;
        }
        return {Value::from_string(out)};
    });
    sfn("is_empty", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        return {Value::from_bool(str_arg(a,0).empty())};
    });
    sfn("reverse", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::string s = str_arg(a,0);
        std::reverse(s.begin(), s.end());
        return {Value::from_string(s)};
    });

    globals_["string"] = str_tbl;
    // Populate string_methods_ so that "hello".upper() works
    for (auto& [k, v] : st->hash) string_methods_[k] = v;
    // Also expose split/join as globals for convenience
    globals_["split"] = st->hash["split"];
    globals_["join"]  = st->hash["join"];

    // ── table table ────────────────────────────────────────────────────────
    Value tbl_mod = Value::from_table();
    auto* tm = tbl_mod.as_table();
    auto tfn = [&](const std::string& key, NativeFunction::Fn fn) {
        tm->set(key, Value::from_native("table." + key, std::move(fn)));
    };

    tfn("len", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty() || !a[0].is_table()) return {Value::from_int(0)};
        // Return the size of the sequential array part (like Lua's #)
        auto* t = a[0].as_table();
        return {Value::from_int((int64_t)t->array.size())};
    });
    // push(tbl, val) — appends to numeric sequence (0,1,2,...)
    tfn("push", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.size() < 2 || !a[0].is_table()) return {};
        auto* t = a[0].as_table();
        int64_t idx = (int64_t)t->array.size(); // append to array part
        t->set_index(idx, a[1]);
        return {};
    });
    // pop(tbl) — removes last numeric element
    tfn("pop", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty() || !a[0].is_table()) return {Value::nil()};
        auto* t = a[0].as_table();
        if (t->array.empty()) return {Value::nil()};
        Value v = t->array.back();
        t->array.pop_back();
        return {v};
    });
    // insert(tbl, idx, val) — insert into array part
    tfn("insert", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.size() < 3 || !a[0].is_table()) return {};
        auto* t = a[0].as_table();
        int64_t idx = a[1].is_int() ? a[1].as_int() : (int64_t)a[1].to_float();
        if (idx < 0 || (size_t)idx > t->array.size()) return {};
        t->array.insert(t->array.begin() + idx, a[2]);
        return {};
    });
    // remove(tbl, idx?) — remove from array part
    tfn("remove", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty() || !a[0].is_table()) return {Value::nil()};
        auto* t = a[0].as_table();
        if (t->array.empty()) return {Value::nil()};
        int64_t idx = (a.size() > 1 && !a[1].is_nil())
            ? (a[1].is_int() ? a[1].as_int() : (int64_t)a[1].to_float())
            : (int64_t)t->array.size() - 1;
        if (idx < 0 || (size_t)idx >= t->array.size()) return {Value::nil()};
        Value removed = t->array[(size_t)idx];
        t->array.erase(t->array.begin() + idx);
        return {removed};
    });
    // keys(tbl) → table (array) of key strings
    tfn("keys", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty() || !a[0].is_table()) return {Value::from_table()};
        Value out = Value::from_table();
        auto* t = a[0].as_table();
        for (auto& [k, v] : t->hash)
            out.as_table()->array.push_back(Value::from_string(k));
        for (size_t i = 0; i < t->array.size(); ++i)
            out.as_table()->array.push_back(Value::from_string(std::to_string(i)));
        return {out};
    });
    // values(tbl) → table (array) of values
    tfn("values", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty() || !a[0].is_table()) return {Value::from_table()};
        Value out = Value::from_table();
        auto* t = a[0].as_table();
        for (auto& [k, v] : t->hash)
            out.as_table()->array.push_back(v);
        for (auto& v : t->array)
            out.as_table()->array.push_back(v);
        return {out};
    });
    // contains(tbl, val) → bool — checks array values AND hash keys
    tfn("contains", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.size() < 2 || !a[0].is_table()) return {Value::from_bool(false)};
        auto* t = a[0].as_table();
        const Value& needle = a[1];
        for (auto& v : t->array)
            if (v == needle) return {Value::from_bool(true)};
        if (needle.is_string())
            return {Value::from_bool(!t->get(needle.as_string()).is_nil())};
        return {Value::from_bool(false)};
    });
    // sort(tbl) — sorts array part in-place
    tfn("sort", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty() || !a[0].is_table()) return {};
        auto& arr = a[0].as_table()->array;
        std::sort(arr.begin(), arr.end(), [](const Value& x, const Value& y) {
            if (x.is_string() && y.is_string()) return x.as_string() < y.as_string();
            if (x.is_int() && y.is_int()) return x.as_int() < y.as_int();
            return x.to_float() < y.to_float();
        });
        return {};
    });
    // copy(tbl) — shallow copy (both hash and array parts)
    tfn("copy", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty() || !a[0].is_table()) return {Value::from_table()};
        Value out = Value::from_table();
        auto* src = a[0].as_table();
        auto* dst = out.as_table();
        for (auto& [k, v] : src->hash) dst->hash[k] = v;
        dst->array = src->array;
        return {out};
    });
    // slice(tbl, start, end?) — return sub-array [start..end)
    tfn("slice", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty() || !a[0].is_table()) return {Value::from_table()};
        auto& arr = a[0].as_table()->array;
        int64_t len   = (int64_t)arr.size();
        int64_t start = a.size() > 1 ? (a[1].is_int() ? a[1].as_int() : (int64_t)a[1].to_float()) : 0;
        int64_t end   = a.size() > 2 ? (a[2].is_int() ? a[2].as_int() : (int64_t)a[2].to_float()) : len;
        if (start < 0) start = std::max<int64_t>(0, len + start);
        if (end   < 0) end   = std::max<int64_t>(0, len + end);
        start = std::min(start, len);
        end   = std::min(end,   len);
        Value out = Value::from_table();
        for (int64_t i = start; i < end; ++i)
            out.as_table()->array.push_back(arr[(size_t)i]);
        return {out};
    });
    // index_of(tbl, val) → int (-1 if not found)
    tfn("index_of", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.size() < 2 || !a[0].is_table()) return {Value::from_int(-1)};
        auto& arr = a[0].as_table()->array;
        const Value& needle = a[1];
        for (size_t i = 0; i < arr.size(); ++i)
            if (arr[i] == needle) return {Value::from_int((int64_t)i)};
        return {Value::from_int(-1)};
    });
    // reverse(tbl) — reverse array part in-place, return self
    tfn("reverse", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty() || !a[0].is_table()) return {Value::nil()};
        std::reverse(a[0].as_table()->array.begin(), a[0].as_table()->array.end());
        return {a[0]};
    });
    // concat(tbl, tbl2) — new array with elements of both
    tfn("concat", [](std::vector<Value> a) -> std::vector<Value> {
        Value out = Value::from_table();
        for (size_t i = 0; i < a.size(); ++i) {
            if (!a[i].is_table()) continue;
            for (auto& v : a[i].as_table()->array)
                out.as_table()->array.push_back(v);
        }
        return {out};
    });
    // map(tbl, fn) — return new array with fn applied to each element
    tfn("map", [this](std::vector<Value> a) -> std::vector<Value> {
        if (a.size() < 2 || !a[0].is_table()) return {Value::from_table()};
        auto& arr = a[0].as_table()->array;
        Value out = Value::from_table();
        for (auto& elem : arr) {
            auto res = invoke_from_native(a[1], {elem});
            out.as_table()->array.push_back(res.empty() ? Value::nil() : res[0]);
        }
        return {out};
    });
    // filter(tbl, fn) — return new array with elements where fn(elem) is truthy
    tfn("filter", [this](std::vector<Value> a) -> std::vector<Value> {
        if (a.size() < 2 || !a[0].is_table()) return {Value::from_table()};
        auto& arr = a[0].as_table()->array;
        Value out = Value::from_table();
        for (auto& elem : arr) {
            auto res = invoke_from_native(a[1], {elem});
            if (!res.empty() && res[0].truthy())
                out.as_table()->array.push_back(elem);
        }
        return {out};
    });
    // reduce(tbl, fn, init) — fold left: acc = fn(acc, elem) for each elem
    tfn("reduce", [this](std::vector<Value> a) -> std::vector<Value> {
        if (a.size() < 2 || !a[0].is_table()) return {Value::nil()};
        auto& arr = a[0].as_table()->array;
        Value acc = a.size() > 2 ? a[2] : Value::nil();
        for (auto& elem : arr) {
            auto res = invoke_from_native(a[1], {acc, elem});
            acc = res.empty() ? Value::nil() : res[0];
        }
        return {acc};
    });
    // each(tbl, fn) — call fn(elem) for each element (returns nil)
    tfn("each", [this](std::vector<Value> a) -> std::vector<Value> {
        if (a.size() < 2 || !a[0].is_table()) return {};
        auto& arr = a[0].as_table()->array;
        for (auto& elem : arr)
            invoke_from_native(a[1], {elem});
        return {};
    });
    // any(tbl, fn) → bool — true if fn(elem) is truthy for at least one element
    tfn("any", [this](std::vector<Value> a) -> std::vector<Value> {
        if (a.size() < 2 || !a[0].is_table()) return {Value::from_bool(false)};
        for (auto& elem : a[0].as_table()->array) {
            auto res = invoke_from_native(a[1], {elem});
            if (!res.empty() && res[0].truthy()) return {Value::from_bool(true)};
        }
        return {Value::from_bool(false)};
    });
    // all(tbl, fn) → bool — true if fn(elem) is truthy for every element
    tfn("all", [this](std::vector<Value> a) -> std::vector<Value> {
        if (a.size() < 2 || !a[0].is_table()) return {Value::from_bool(true)};
        for (auto& elem : a[0].as_table()->array) {
            auto res = invoke_from_native(a[1], {elem});
            if (res.empty() || !res[0].truthy()) return {Value::from_bool(false)};
        }
        return {Value::from_bool(true)};
    });
    // flat(tbl) — flatten one level (array of arrays → flat array)
    tfn("flat", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty() || !a[0].is_table()) return {Value::from_table()};
        Value out = Value::from_table();
        for (auto& elem : a[0].as_table()->array) {
            if (elem.is_table()) {
                for (auto& inner : elem.as_table()->array)
                    out.as_table()->array.push_back(inner);
            } else {
                out.as_table()->array.push_back(elem);
            }
        }
        return {out};
    });
    // first(tbl) → value or nil
    tfn("first", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty() || !a[0].is_table() || a[0].as_table()->array.empty())
            return {Value::nil()};
        return {a[0].as_table()->array.front()};
    });
    // last(tbl) → value or nil
    tfn("last", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty() || !a[0].is_table() || a[0].as_table()->array.empty())
            return {Value::nil()};
        return {a[0].as_table()->array.back()};
    });

    globals_["table"] = tbl_mod;
    // Populate table_methods_ so that arr.push(x) works
    for (auto& [k, v] : tm->hash) table_methods_[k] = v;

    // ── io table ───────────────────────────────────────────────────────────
    Value io_tbl = Value::from_table();
    auto* it = io_tbl.as_table();
    auto ifn = [&](const std::string& key, NativeFunction::Fn fn) {
        it->set(key, Value::from_native("io." + key, std::move(fn)));
    };

    ifn("read_file", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::string path = str_arg(a, 0);
        std::ifstream f(path, std::ios::binary);
        if (!f) return {Value::nil()};
        std::ostringstream ss; ss << f.rdbuf();
        return {Value::from_string(ss.str())};
    });
    ifn("write_file", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::string path = str_arg(a, 0);
        std::string data = str_arg(a, 1);
        std::ofstream f(path, std::ios::binary);
        if (!f) return {Value::from_bool(false)};
        f << data;
        return {Value::from_bool(true)};
    });
    ifn("append_file", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::string path = str_arg(a, 0);
        std::string data = str_arg(a, 1);
        std::ofstream f(path, std::ios::binary | std::ios::app);
        if (!f) return {Value::from_bool(false)};
        f << data;
        return {Value::from_bool(true)};
    });
    ifn("read_line", [](std::vector<Value> /*a*/) -> std::vector<Value> {
        std::string line;
        if (!std::getline(std::cin, line)) return {Value::nil()};
        return {Value::from_string(line)};
    });
    ifn("print_err", [](std::vector<Value> a) -> std::vector<Value> {
        for (auto& v : a) std::cerr << v.to_string();
        std::cerr << '\n';
        return {};
    });
    ifn("exists", [&str_arg](std::vector<Value> a) -> std::vector<Value> {
        std::ifstream f(str_arg(a, 0));
        return {Value::from_bool(f.good())};
    });

    globals_["io"] = io_tbl;

    // ── json table ─────────────────────────────────────────────────────────
    Value json_tbl = Value::from_table();
    auto* jt = json_tbl.as_table();
    auto jfn = [&](const std::string& key, NativeFunction::Fn fn) {
        jt->set(key, Value::from_native("json." + key, std::move(fn)));
    };

    // json.encode(value) -> string
    jfn("encode", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty()) return {Value::from_string("null")};

        std::function<std::string(const Value&, int)> encode_val;
        encode_val = [&](const Value& v, int depth) -> std::string {
            if (depth > 64) return "null"; // guard against circular structures
            if (v.is_nil())   return "null";
            if (v.is_bool())  return v.as_bool() ? "true" : "false";
            if (v.is_int())   return std::to_string(v.as_int());
            if (v.is_float()) {
                double d = v.as_float();
                if (std::isinf(d) || std::isnan(d)) return "null";
                std::ostringstream os;
                os << std::setprecision(17) << d;
                std::string s = os.str();
                // Ensure there is a decimal point so JSON consumers know it is a float
                if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
                    s += ".0";
                return s;
            }
            if (v.is_string()) {
                const std::string& s = v.as_string();
                std::string out;
                out += '"';
                for (unsigned char ch : s) {
                    switch (ch) {
                        case '"':  out += "\\\""; break;
                        case '\\': out += "\\\\"; break;
                        case '\n': out += "\\n";  break;
                        case '\r': out += "\\r";  break;
                        case '\t': out += "\\t";  break;
                        default:
                            if (ch < 0x20) {
                                char buf[8];
                                snprintf(buf, sizeof(buf), "\\u%04x", ch);
                                out += buf;
                            } else {
                                out += (char)ch;
                            }
                    }
                }
                out += '"';
                return out;
            }
            if (v.is_table()) {
                auto* tbl = v.as_table();
                // Detect array: has no hash keys, or keys are all sequential ints
                bool is_array = !tbl->array.empty() && tbl->hash.empty();
                if (is_array) {
                    std::string out = "[";
                    for (size_t i = 0; i < tbl->array.size(); ++i) {
                        if (i > 0) out += ',';
                        out += encode_val(tbl->array[i], depth + 1);
                    }
                    out += ']';
                    return out;
                }
                // Object (hash map — skip internal __ keys)
                std::string out = "{";
                bool first = true;
                for (auto& [k, val] : tbl->hash) {
                    if (k.size() >= 2 && k[0] == '_' && k[1] == '_') continue;
                    if (!first) out += ',';
                    first = false;
                    out += '"';
                    out += k;
                    out += "\":";
                    out += encode_val(val, depth + 1);
                }
                out += '}';
                return out;
            }
            return "null"; // function / userdata
        };

        return {Value::from_string(encode_val(a[0], 0))};
    });

    // json.decode(string) -> value
    jfn("decode", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty() || !a[0].is_string()) return {Value::nil()};
        const std::string& src = a[0].as_string();
        size_t pos = 0;

        std::function<Value()> parse_val;
        auto skip_ws = [&]() {
            while (pos < src.size() &&
                   (src[pos] == ' ' || src[pos] == '\t' ||
                    src[pos] == '\n' || src[pos] == '\r')) ++pos;
        };
        auto parse_string = [&]() -> std::string {
            ++pos; // skip opening "
            std::string out;
            while (pos < src.size() && src[pos] != '"') {
                if (src[pos] == '\\' && pos + 1 < src.size()) {
                    ++pos;
                    switch (src[pos]) {
                        case '"':  out += '"';  break;
                        case '\\': out += '\\'; break;
                        case '/':  out += '/';  break;
                        case 'n':  out += '\n'; break;
                        case 'r':  out += '\r'; break;
                        case 't':  out += '\t'; break;
                        case 'u': {
                            // \uXXXX — decode to UTF-8 (BMP only)
                            if (pos + 4 < src.size()) {
                                unsigned cp = std::stoul(src.substr(pos + 1, 4), nullptr, 16);
                                pos += 4;
                                if (cp < 0x80) {
                                    out += (char)cp;
                                } else if (cp < 0x800) {
                                    out += (char)(0xC0 | (cp >> 6));
                                    out += (char)(0x80 | (cp & 0x3F));
                                } else {
                                    out += (char)(0xE0 | (cp >> 12));
                                    out += (char)(0x80 | ((cp >> 6) & 0x3F));
                                    out += (char)(0x80 | (cp & 0x3F));
                                }
                            }
                            break;
                        }
                        default: out += src[pos]; break;
                    }
                } else {
                    out += src[pos];
                }
                ++pos;
            }
            if (pos < src.size()) ++pos; // closing "
            return out;
        };

        parse_val = [&]() -> Value {
            skip_ws();
            if (pos >= src.size()) return Value::nil();
            char c = src[pos];

            if (c == '"') return Value::from_string(parse_string());

            if (c == '[') {
                ++pos;
                Value tbl = Value::from_table();
                auto* t = tbl.as_table();
                skip_ws();
                if (pos < src.size() && src[pos] == ']') { ++pos; return tbl; }
                while (pos < src.size()) {
                    t->array.push_back(parse_val());
                    skip_ws();
                    if (pos < src.size() && src[pos] == ',') { ++pos; skip_ws(); }
                    else break;
                }
                if (pos < src.size() && src[pos] == ']') ++pos;
                return tbl;
            }

            if (c == '{') {
                ++pos;
                Value tbl = Value::from_table();
                auto* t = tbl.as_table();
                skip_ws();
                if (pos < src.size() && src[pos] == '}') { ++pos; return tbl; }
                while (pos < src.size()) {
                    skip_ws();
                    if (src[pos] != '"') break;
                    std::string key = parse_string();
                    skip_ws();
                    if (pos < src.size() && src[pos] == ':') ++pos;
                    Value val = parse_val();
                    t->set(key, val);
                    skip_ws();
                    if (pos < src.size() && src[pos] == ',') { ++pos; skip_ws(); }
                    else break;
                }
                if (pos < src.size() && src[pos] == '}') ++pos;
                return tbl;
            }

            if (src.substr(pos, 4) == "true")  { pos += 4; return Value::from_bool(true);  }
            if (src.substr(pos, 5) == "false") { pos += 5; return Value::from_bool(false); }
            if (src.substr(pos, 4) == "null")  { pos += 4; return Value::nil();             }

            // number
            size_t start = pos;
            if (src[pos] == '-') ++pos;
            while (pos < src.size() && std::isdigit((unsigned char)src[pos])) ++pos;
            bool is_float = false;
            if (pos < src.size() && src[pos] == '.') {
                is_float = true; ++pos;
                while (pos < src.size() && std::isdigit((unsigned char)src[pos])) ++pos;
            }
            if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
                is_float = true; ++pos;
                if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) ++pos;
                while (pos < src.size() && std::isdigit((unsigned char)src[pos])) ++pos;
            }
            std::string num_str = src.substr(start, pos - start);
            if (num_str.empty()) return Value::nil();
            if (is_float) return Value::from_float(std::stod(num_str));
            return Value::from_int(std::stoll(num_str));
        };

        Value result = parse_val();
        return {result};
    });

    globals_["json"] = json_tbl;
}

// ===========================================================================
// Type method helpers
// ===========================================================================
Value VM::make_bound_method(const Value& method, Value self_val) {
    // Return a native that prepends self_val to the arg list then calls method.
    auto nat = method.as_native();
    auto fn   = nat->fn;
    std::string name = nat->name;
    return Value::from_native(name, [fn, self_val](std::vector<Value> args) mutable {
        args.insert(args.begin(), self_val);
        return fn(std::move(args));
    });
}

// ===========================================================================
// Error helpers
// ===========================================================================
void VM::runtime_error(const std::string& msg) {
    last_error_ = {msg, format_trace()};
    throw last_error_;
}

std::string VM::format_trace() const {
    std::ostringstream ss;
    for (int i = (int)frames_.size() - 1; i >= 0; --i) {
        auto& f = frames_[i];
        size_t line = (f.pc > 0 && f.pc - 1 < f.proto->lines.size())
                      ? f.proto->lines[f.pc - 1] : 0;
        ss << "  in " << (f.proto->name.empty() ? "<anon>" : f.proto->name)
           << " line " << line << "\n";
    }
    return ss.str();
}

// ===========================================================================
// Execute a compiled chunk
// ===========================================================================
bool VM::execute(Chunk& chunk) {
    if (!chunk.main_proto) {
        last_error_ = {"empty chunk", ""};
        return false;
    }
    // Push main frame at base reg 0
    frames_.clear();
    frames_.push_back({chunk.main_proto, 0, 0});
    try {
        return run();
    } catch (const RuntimeError& e) {
        last_error_ = e;
        return false;
    } catch (const std::exception& e) {
        last_error_ = {e.what(), ""};
        return false;
    }
}

// ===========================================================================
// Call a global function
// ===========================================================================
Value VM::call_global(const std::string& name, std::vector<Value> args) {
    Value fn = get_global(name);
    if (fn.is_nil()) {
        runtime_error("undefined global function '" + name + "'");
    }
    if (fn.is_native()) {
        auto results = fn.as_native()->fn(std::move(args));
        return results.empty() ? Value::nil() : results[0];
    }
    if (fn.is_closure()) {
        // Place callee at reg 0, args at reg 1..n
        regs_[0] = fn;
        for (size_t i = 0; i < args.size(); ++i)
            regs_[1 + i] = args[i];

        frames_.clear();
        // Push the callee frame directly (base_reg=1 so args are reg 0..n-1 inside)
        Proto* proto = fn.as_closure()->proto;
        CallFrame frame;
        frame.proto    = proto;
        frame.pc       = 0;
        frame.base_reg = 1;  // args start at regs_[1]
        frames_.push_back(frame);
        // Fill missing params with nil
        for (uint8_t i = (uint8_t)args.size(); i < proto->num_params; ++i)
            regs_[1 + i] = Value::nil();

        try {
            run();
            // Return value stored at regs_[0] by Op::Return (this_base-1 = 1-1 = 0)
            return regs_[0];
        } catch (const RuntimeError& e) {
            last_error_ = e;
            return Value::nil();
        }
    }
    runtime_error("'" + name + "' is not callable");
    return Value::nil();
}

// ===========================================================================
// Call instruction handler
// ===========================================================================
bool VM::call(uint8_t base_reg_offset, uint8_t num_args, uint8_t num_results) {
    uint8_t abs_base = cur_frame().base_reg + base_reg_offset;
    Value& callee = regs_[abs_base];

    if (callee.is_native()) {
        std::vector<Value> args;
        for (int i = 0; i < num_args; ++i) {
            args.push_back(regs_[abs_base + 1 + i]);
        }
        auto results = callee.as_native()->fn(std::move(args));
        // Store results
        for (uint8_t i = 0; i < num_results && i < results.size(); ++i) {
            regs_[abs_base + i] = results[i];
        }
        for (uint8_t i = (uint8_t)results.size(); i < num_results; ++i) {
            regs_[abs_base + i] = Value::nil();
        }
        return true;
    }

    if (callee.is_closure()) {
        ZClosure* cl = callee.as_closure();
        Proto* proto = cl->proto;
        if (frames_.size() >= MAX_FRAMES) {
            runtime_error("stack overflow");
        }
        CallFrame frame;
        frame.proto       = proto;
        frame.pc          = 0;
        frame.base_reg    = abs_base + 1; // args start at abs_base+1
        frame.num_results = num_results;
        frame.closure     = cl;
        frames_.push_back(frame);
        // The callee's registers start at abs_base+1.
        // Parameters are already in abs_base+1..abs_base+num_args.
        if (proto->is_vararg && proto->num_params > 0) {
            uint8_t fixed = proto->num_params - 1;
            // Bundle extra args into an array table at the vararg register.
            Value varargs = Value::from_table();
            auto* vt = varargs.as_table();
            for (uint8_t i = fixed; i < num_args; ++i)
                vt->set_index((int64_t)(i - fixed), regs_[frame.base_reg + i]);
            vt->set("n", Value::from_int((int64_t)(num_args > fixed ? num_args - fixed : 0)));
            // Fill fixed params with nil if under-supplied
            for (uint8_t i = num_args; i < fixed; ++i)
                regs_[frame.base_reg + i] = Value::nil();
            regs_[frame.base_reg + fixed] = std::move(varargs);
        } else {
            for (uint8_t i = num_args; i < proto->num_params; ++i)
                regs_[frame.base_reg + i] = Value::nil();
        }
        return true; // run() will execute the new frame
    }

    // ── Class instantiation: calling a table with __class__ ──────────────────
    // When a class prototype table is called (e.g. Animal("Rex")), we:
    //   1. Create a fresh instance table
    //   2. Copy all method closures from the prototype
    //   3. Set inst.__class__ to the class name
    //   4. Call inst.init(args...) if it exists
    //   5. Store the instance in the result register
    if (callee.is_table()) {
        Value cls_val = callee.as_table()->get("__class__");
        if (cls_val.is_string()) {
            // Build instance
            Value inst = Value::from_table();
            auto* proto_tbl = callee.as_table();
            auto* inst_tbl  = inst.as_table();
            // Determine which members are static (should NOT be copied to instances).
            ZTable* statics_tbl = nullptr;
            Value statics_val = proto_tbl->get("__statics__");
            if (statics_val.is_table()) statics_tbl = statics_val.as_table();

            // Copy instance members (skip __class__, __base__, __statics__, and statics).
            for (auto& [k, v] : proto_tbl->hash) {
                if (k == "__class__" || k == "__base__" || k == "__statics__") continue;
                if (statics_tbl && statics_tbl->get(k).truthy()) continue;
                inst_tbl->set(k, v);
            }
            inst_tbl->set("__class__", cls_val);

            // Call init if present, passing instance as self + caller's args
            Value init_fn = inst_tbl->get("init");
            if (init_fn.is_closure()) {
                Proto* init_proto = init_fn.as_closure()->proto;
                if (frames_.size() >= MAX_FRAMES) runtime_error("stack overflow");

                // Shift user args up by one to make room for self at slot 1.
                // Caller placed args at abs_base+1..abs_base+num_args.
                // We need: [method=abs_base, self=abs_base+1, arg0=abs_base+2, ...]
                for (int i = num_args; i >= 1; --i)
                    regs_[abs_base + 1 + i] = regs_[abs_base + i];
                regs_[abs_base + 1] = inst; // self

                CallFrame frame;
                frame.proto       = init_proto;
                frame.pc          = 0;
                frame.base_reg    = abs_base + 1; // self is reg 0 inside init
                frame.num_results = 1;
                frame.closure     = init_fn.as_closure();
                frames_.push_back(frame);
                // Fill missing params with nil
                for (uint8_t i = (uint8_t)(num_args + 1);
                     i < init_proto->num_params; ++i)
                    regs_[frame.base_reg + i] = Value::nil();

                // After init returns, its Return handler writes nil to abs_base.
                // Record the pending constructor so we can restore inst there.
                ctor_stack_.push_back({abs_base, inst});
                return true; // run() will execute init frame
            }

            // No init — just return the instance directly
            regs_[abs_base] = inst;
            return true;
        }
    }

    runtime_error("attempt to call non-callable value (type: " + callee.type_name() + ")");
    return false;
}

// ===========================================================================
// call_method — method call: callee=R[A], self=R[A+1], user_args=R[A+2..A+1+B]
// For native callees: self is skipped, only user args are passed.
// For closure callees: frame.base_reg = abs_A+1 (self=R[0], user_args=R[1..B]).
// ===========================================================================
bool VM::call_method(uint8_t base_reg_offset, uint8_t user_args, uint8_t num_results) {
    uint8_t abs_A    = cur_frame().base_reg + base_reg_offset;
    Value&  callee   = regs_[abs_A];
    uint8_t self_abs = abs_A + 1;  // self is always at abs_A+1

    if (callee.is_native()) {
        // Native: pass only user args (skip self)
        std::vector<Value> args;
        for (uint8_t i = 0; i < user_args; ++i)
            args.push_back(regs_[abs_A + 2 + i]);
        auto results = callee.as_native()->fn(std::move(args));
        for (uint8_t i = 0; i < num_results && i < results.size(); ++i)
            regs_[abs_A + i] = results[i];
        for (uint8_t i = (uint8_t)results.size(); i < num_results; ++i)
            regs_[abs_A + i] = Value::nil();
        return true;
    }

    if (callee.is_closure()) {
        ZClosure* cl = callee.as_closure();
        Proto* proto = cl->proto;
        if (frames_.size() >= MAX_FRAMES) runtime_error("stack overflow");

        // If num_params == user_args, the closure was declared as a standalone
        // function (no self parameter).  Call it without self: args are at
        // abs_A+2..abs_A+1+user_args, shift them down to abs_A+1.
        if (proto->num_params == user_args) {
            for (uint8_t i = 0; i < user_args; ++i)
                regs_[abs_A + 1 + i] = regs_[abs_A + 2 + i];
            return call(base_reg_offset, user_args, num_results);
        }

        CallFrame frame;
        frame.proto       = proto;
        frame.pc          = 0;
        frame.base_reg    = self_abs;  // self at R[0], user_args at R[1..user_args]
        frame.num_results = num_results;
        frame.closure     = cl;
        frames_.push_back(frame);
        // Fill missing params with nil (param 0 = self already in place)
        for (uint8_t i = user_args + 1; i < proto->num_params; ++i)
            regs_[self_abs + i] = Value::nil();
        return true;
    }

    // Table with __class__ — should not normally arrive via CallMethod, but handle gracefully
    if (callee.is_table()) {
        // Treat as plain call with user_args args at abs_A+2..
        // Shift args down by 1 to abs_A+1 so call() sees them correctly
        for (uint8_t i = 0; i < user_args; ++i)
            regs_[abs_A + 1 + i] = regs_[abs_A + 2 + i];
        return call(base_reg_offset, user_args, num_results);
    }

    runtime_error("attempt to call non-callable value (type: " + callee.type_name() + ")");
    return false;
}

// ===========================================================================
// Upvalue helpers
// ===========================================================================
std::shared_ptr<Value> VM::capture_reg(uint16_t abs_reg) {
    auto it = open_upvals_.find(abs_reg);
    if (it != open_upvals_.end()) {
        // Sync cell to current register value before returning
        *it->second = regs_[abs_reg];
        return it->second;
    }
    auto cell = std::make_shared<Value>(regs_[abs_reg]);
    open_upvals_[abs_reg] = cell;
    return cell;
}

void VM::close_upvals_above(uint8_t base) {
    // Snapshot current register values into cells, then remove from open set.
    for (auto it = open_upvals_.begin(); it != open_upvals_.end(); ) {
        if (it->first >= base) {
            // value already snapshotted at capture/sync time; just remove
            it = open_upvals_.erase(it);
        } else {
            ++it;
        }
    }
}

// ===========================================================================
// Main interpreter loop
// ===========================================================================
bool VM::run(size_t stop_depth) {
    while (frames_.size() > stop_depth) {
        // Snapshot frame state — re-snapshot whenever the frame stack changes.
        Proto*   proto    = frames_.back().proto;
        uint8_t  base_reg = frames_.back().base_reg;
        size_t&  pc       = frames_.back().pc;

        bool frame_changed = false;

        try {
        while (!frame_changed && pc < proto->code.size()) {
            uint32_t instr = proto->code[pc++];
            Op       op  = instr_op(instr);
            uint8_t  A   = instr_A(instr);
            uint8_t  B   = instr_B(instr);
            uint8_t  C   = instr_C(instr);
            uint16_t Bx  = instr_Bx(instr);
            int32_t  sbx = instr_sBx(instr);

            // Helper: absolute register index using snapshotted base_reg
            auto R = [&](uint8_t r) -> Value& {
                return regs_[base_reg + r];
            };
            auto K = [&](uint16_t k) -> const Value& {
                return proto->constants[k];
            };

            switch (op) {
                case Op::LoadNil:
                    R(A) = Value::nil();
                    break;

                case Op::LoadBool:
                    R(A) = Value::from_bool(B != 0);
                    break;

                case Op::LoadInt:
                    R(A) = Value::from_int(sbx);
                    break;

                case Op::LoadK:
                    R(A) = K(Bx);
                    break;

                case Op::Move:
                    R(A) = R(B);
                    break;

                case Op::GetGlobal: {
                    const std::string& name = K(Bx).as_string();
                    auto it = globals_.find(name);
                    R(A) = (it != globals_.end()) ? it->second : Value::nil();
                    break;
                }

                case Op::SetGlobal: {
                    const std::string& name = K(Bx).as_string();
                    globals_[name] = R(A);
                    break;
                }

                case Op::NewTable:
                    R(A) = Value::from_table();
                    break;

                case Op::Inherit: {
                    // A = child table, B = parent table (nil = no-op)
                    // Copy all entries from parent into child (skip metadata and statics).
                    Value& child  = R(A);
                    Value& parent = R(B);
                    if (parent.is_nil()) break; // no-op for trait with no defaults
                    if (child.is_table() && parent.is_table()) {
                        auto* ct = child.as_table();
                        auto* pt = parent.as_table();
                        // Determine parent statics so we don't inherit them as instance methods.
                        ZTable* parent_statics = nullptr;
                        Value ps_val = pt->get("__statics__");
                        if (ps_val.is_table()) parent_statics = ps_val.as_table();
                        for (auto& [k, v] : pt->hash) {
                            if (k == "__class__" || k == "__base__" || k == "__statics__") continue;
                            if (parent_statics && parent_statics->get(k).truthy()) continue;
                            ct->set(k, v);
                        }
                    }
                    break;
                }

                case Op::GetField: {
                    // Encoding: A=dest, upper8(Bx)=obj_reg, lower8(Bx)=name_k
                    uint8_t  obj_r  = (uint8_t)(Bx >> 8);
                    uint8_t  name_k = (uint8_t)(Bx & 0xFF);
                    const std::string& field = K(name_k).as_string();
                    Value obj_snap = R(obj_r); // snapshot before any regs_ mutation
                    if (obj_snap.is_table()) {
                        Value found = obj_snap.as_table()->get(field);
                        if (!found.is_nil()) {
                            R(A) = std::move(found);
                        } else {
                            // Fall back to table type methods (e.g. arr.push)
                            auto it = table_methods_.find(field);
                            if (it != table_methods_.end())
                                R(A) = make_bound_method(it->second, obj_snap);
                            else
                                R(A) = Value::nil();
                        }
                    } else if (obj_snap.tag == Value::Tag::String) {
                        auto it = string_methods_.find(field);
                        if (it != string_methods_.end())
                            R(A) = make_bound_method(it->second, obj_snap);
                        else
                            R(A) = Value::nil();
                    } else if (obj_snap.is_nil()) {
                        runtime_error("attempt to index nil value");
                    } else {
                        R(A) = Value::nil();
                    }
                    break;
                }

                case Op::SetField: {
                    // Encoding: A=obj_reg, upper8(Bx)=name_k, lower8(Bx)=val_reg
                    uint8_t  name_k = (uint8_t)((Bx >> 8) & 0xFF);
                    uint8_t  val_r  = (uint8_t)(Bx & 0xFF);
                    Value& obj = R(A);
                    if (!obj.is_table()) runtime_error("attempt to set field on non-table");
                    const std::string& field = K(name_k).as_string();
                    obj.as_table()->set(field, R(val_r));
                    break;
                }

                case Op::GetIndex: {
                    Value& obj = R(B);
                    Value& idx = R(C);
                    if (obj.is_table()) {
                        if (idx.is_int())    R(A) = obj.as_table()->get_index(idx.as_int());
                        else if (idx.is_string()) R(A) = obj.as_table()->get(idx.as_string());
                        else R(A) = Value::nil();
                    } else {
                        runtime_error("attempt to index non-table value");
                    }
                    break;
                }

                case Op::SetIndex: {
                    Value& obj = R(A);
                    Value& idx = R(B);
                    Value& val = R(C);
                    if (!obj.is_table()) runtime_error("attempt to index non-table value");
                    if (idx.is_int())    obj.as_table()->set_index(idx.as_int(), val);
                    else if (idx.is_string()) obj.as_table()->set(idx.as_string(), val);
                    break;
                }

                // --- arithmetic ---
                case Op::Add: {
                    Value& lv = R(B); Value& rv = R(C);
                    if (lv.is_int() && rv.is_int())
                        R(A) = Value::from_int(lv.as_int() + rv.as_int());
                    else if (lv.is_number() && rv.is_number())
                        R(A) = Value::from_float(lv.to_float() + rv.to_float());
                    else if (lv.is_string() && rv.is_string())
                        R(A) = Value::from_string(lv.as_string() + rv.as_string());
                    else runtime_error("type error in '+': " + lv.type_name() + " + " + rv.type_name());
                    break;
                }
                case Op::Sub: {
                    Value& lv = R(B); Value& rv = R(C);
                    if (lv.is_int() && rv.is_int())
                        R(A) = Value::from_int(lv.as_int() - rv.as_int());
                    else if (lv.is_number() && rv.is_number())
                        R(A) = Value::from_float(lv.to_float() - rv.to_float());
                    else runtime_error("type error in '-'");
                    break;
                }
                case Op::Mul: {
                    Value& lv = R(B); Value& rv = R(C);
                    if (lv.is_int() && rv.is_int())
                        R(A) = Value::from_int(lv.as_int() * rv.as_int());
                    else if (lv.is_number() && rv.is_number())
                        R(A) = Value::from_float(lv.to_float() * rv.to_float());
                    else runtime_error("type error in '*'");
                    break;
                }
                case Op::Div: {
                    Value& lv = R(B); Value& rv = R(C);
                    if (!lv.is_number() || !rv.is_number())
                        runtime_error("type error in '/'");
                    R(A) = Value::from_float(lv.to_float() / rv.to_float());
                    break;
                }
                case Op::Mod: {
                    Value& lv = R(B); Value& rv = R(C);
                    if (lv.is_int() && rv.is_int())
                        R(A) = Value::from_int(lv.as_int() % rv.as_int());
                    else if (lv.is_number() && rv.is_number())
                        R(A) = Value::from_float(std::fmod(lv.to_float(), rv.to_float()));
                    else runtime_error("type error in '%'");
                    break;
                }
                case Op::Neg: {
                    Value& v = R(B);
                    if (v.is_int())   R(A) = Value::from_int(-v.as_int());
                    else if (v.is_float()) R(A) = Value::from_float(-v.as_float());
                    else runtime_error("type error in unary '-'");
                    break;
                }

                // --- comparison ---
                case Op::Eq:
                    R(A) = Value::from_bool(R(B) == R(C));
                    break;
                case Op::Ne:
                    R(A) = Value::from_bool(R(B) != R(C));
                    break;
                case Op::Lt: {
                    Value& lv = R(B); Value& rv = R(C);
                    if (lv.is_int() && rv.is_int())
                        R(A) = Value::from_bool(lv.as_int() < rv.as_int());
                    else if (lv.is_number() && rv.is_number())
                        R(A) = Value::from_bool(lv.to_float() < rv.to_float());
                    else runtime_error("type error in '<'");
                    break;
                }
                case Op::Le: {
                    Value& lv = R(B); Value& rv = R(C);
                    if (lv.is_int() && rv.is_int())
                        R(A) = Value::from_bool(lv.as_int() <= rv.as_int());
                    else if (lv.is_number() && rv.is_number())
                        R(A) = Value::from_bool(lv.to_float() <= rv.to_float());
                    else runtime_error("type error in '<='");
                    break;
                }

                // --- logic ---
                case Op::Not:
                    R(A) = Value::from_bool(!R(B).truthy());
                    break;
                case Op::And:
                    R(A) = Value::from_bool(R(B).truthy() && R(C).truthy());
                    break;
                case Op::Or:
                    R(A) = Value::from_bool(R(B).truthy() || R(C).truthy());
                    break;

                // --- control flow ---
                case Op::Jump:
                    pc = (size_t)((int)pc + sbx);
                    break;
                case Op::JumpFalse:
                    if (!R(A).truthy()) pc = (size_t)((int)pc + sbx);
                    break;
                case Op::JumpTrue:
                    if (R(A).truthy()) pc = (size_t)((int)pc + sbx);
                    break;

                // --- functions ---
                case Op::Closure: {
                    // Bx = index into proto->protos
                    if (Bx >= proto->protos.size())
                        runtime_error("invalid closure index");
                    Proto* fn_proto = proto->protos[Bx];
                    R(A) = Value::from_closure(fn_proto);
                    ZClosure* cl = R(A).as_closure();
                    // Read one pseudo-instruction per upvalue in fn_proto->upvalues.
                    // Op::Move     A=0 B=reg  → capture local register from enclosing frame
                    // Op::GetUpval A=0 B=idx  → inherit upvalue from enclosing closure
                    cl->upvalues.resize(fn_proto->upvalues.size());
                    for (size_t uvi = 0; uvi < fn_proto->upvalues.size(); ++uvi) {
                        uint32_t pseudo = proto->code[pc++];
                        Op pseudo_op    = instr_op(pseudo);
                        uint8_t  pseudo_B = instr_B(pseudo);
                        if (pseudo_op == Op::Move) {
                            // capture local: create/reuse a shared cell for this reg
                            cl->upvalues[uvi] = capture_reg(
                                (uint16_t)(base_reg + pseudo_B));
                        } else {
                            // inherit upvalue from enclosing closure
                            CallFrame& cf = frames_.back();
                            if (cf.closure && pseudo_B < cf.closure->upvalues.size())
                                cl->upvalues[uvi] = cf.closure->upvalues[pseudo_B];
                            else
                                cl->upvalues[uvi] = std::make_shared<Value>(Value::nil());
                        }
                    }
                    break;
                }

                case Op::GetUpval: {
                    // A=dest, B=upvalue index
                    ZClosure* cl = frames_.back().closure;
                    if (cl && B < cl->upvalues.size())
                        R(A) = *cl->upvalues[B];
                    else
                        R(A) = Value::nil();
                    break;
                }

                case Op::SetUpval: {
                    // A=src, B=upvalue index
                    ZClosure* cl = frames_.back().closure;
                    if (cl && B < cl->upvalues.size())
                        *cl->upvalues[B] = R(A);
                    break;
                }

                case Op::Call: {
                    // A=callee_reg, B=num_args, C=num_results
                    size_t frames_before = frames_.size();
                    call(A, B, C);
                    if (frames_.size() > frames_before) {
                        frame_changed = true; // new frame pushed — break inner loop
                    }
                    break;
                }

                case Op::CallMethod: {
                    // A=callee_reg, B=user_args, C=num_results
                    // Layout: R[A]=method, R[A+1]=self, R[A+2..A+1+B]=user args
                    // For closures: self becomes R[0] inside the frame (base_reg=abs_A+1)
                    // For natives: self is skipped, only user args are passed
                    size_t frames_before = frames_.size();
                    call_method(A, B, C);
                    if (frames_.size() > frames_before) {
                        frame_changed = true;
                    }
                    break;
                }

                case Op::Return: {
                    // A=first_result_reg (relative to base_reg), B=count (0 = nil)
                    uint8_t this_base  = base_reg;
                    uint8_t nr         = frames_.back().num_results;
                    close_upvals_above(this_base);
                    frames_.pop_back();
                    frame_changed = true;
                    // Now frame/proto are dangling — do not access them again.
                    if (!frames_.empty()) {
                        uint8_t dest = this_base - 1;
                        for (uint8_t i = 0; i < nr; ++i) {
                            regs_[dest + i] = (B > 0 && i < B)
                                ? regs_[this_base + A + i]
                                : Value::nil();
                        }
                    } else {
                        regs_[0] = (B > 0) ? regs_[this_base + A] : Value::nil();
                    }
                    // If this was an init() frame for a constructor, restore instance.
                    if (!ctor_stack_.empty() &&
                        ctor_stack_.back().result_reg == (uint8_t)(this_base - 1)) {
                        regs_[ctor_stack_.back().result_reg] = ctor_stack_.back().inst;
                        ctor_stack_.pop_back();
                    }
                    break;
                }

                case Op::Concat: {
                    std::string s = R(B).to_string() + R(C).to_string();
                    R(A) = Value::from_string(s);
                    break;
                }

                case Op::CheckNil: {
                    if (R(A).is_nil()) {
                        const std::string& field = (Bx < proto->constants.size())
                            ? proto->constants[Bx].to_string() : "?";
                        runtime_error("force unwrap on nil (accessing '" + field + "')");
                    }
                    break;
                }

                case Op::Import: {
                    // Bx = constant index of module name string
                    const std::string& mod_name = proto->constants[Bx].to_string();
                    std::string err;
                    // execute_module clears frames_ internally; save/restore them
                    // so the calling script can continue after the import.
                    auto saved_frames = std::move(frames_);
                    Module* mod = loader_.load(mod_name, *this, err);
                    frames_ = std::move(saved_frames);
                    frame_changed = true; // force re-snapshot on next outer iteration
                    if (!mod) runtime_error("import failed: " + err);
                    // Build a table from the module's exports and put it in R[A]
                    Value tbl = Value::from_table();
                    for (auto& [k, v] : mod->exports)
                        tbl.as_table()->set(k, v);
                    R(A) = std::move(tbl);
                    break;
                }

                case Op::TLen: {
                    auto& src = R(B);
                    if (src.tag == Value::Tag::Table) {
                        R(A) = Value::from_int((int64_t)src.table_ptr->array.size());
                    } else if (src.tag == Value::Tag::String) {
                        R(A) = Value::from_int((int64_t)src.str_ptr->data.size());
                    } else {
                        runtime_error("'#' operator requires table or string, got " + src.type_name());
                    }
                    break;
                }

                case Op::Pow: {
                    auto& lb = R(B); auto& rc = R(C);
                    if (lb.is_int() && rc.is_int() && rc.as_int() >= 0) {
                        int64_t base = lb.as_int(), exp = rc.as_int(), result = 1;
                        for (int64_t i = 0; i < exp; ++i) result *= base;
                        R(A) = Value::from_int(result);
                    } else {
                        R(A) = Value::from_float(std::pow(lb.to_float(), rc.to_float()));
                    }
                    break;
                }

                case Op::TableKeys: {
                    Value& tbl = R(B);
                    if (!tbl.is_table()) runtime_error("'for k,v' requires a table");
                    Value keys = Value::from_table();
                    auto* kt = keys.as_table();
                    int64_t i = 0;
                    for (auto& [k, v] : tbl.as_table()->hash)
                        kt->set_index(i++, Value::from_string(k));
                    R(A) = std::move(keys);
                    break;
                }

                case Op::Throw: {
                    thrown_value_ = R(A);
                    runtime_error(R(A).to_string());
                    break;
                }

                case Op::PushTry: {
                    // A = catch_reg (relative to base_reg), sBx = offset to catch block
                    size_t catch_pc = pc + (size_t)sbx; // pc already incremented past this instr
                    try_stack_.push_back({frames_.size(), catch_pc, base_reg, A});
                    break;
                }

                case Op::PopTry: {
                    if (!try_stack_.empty()) try_stack_.pop_back();
                    break;
                }

                case Op::IsInstance: {
                    // A=dest, B=object, C=const_idx (class or trait name string)
                    const std::string& target = K(C).as_string();
                    const std::string  trait_key = "__trait_" + target + "__";
                    Value& obj = R(B);
                    bool result = false;
                    if (obj.is_table()) {
                        auto* tbl = obj.as_table();
                        auto it = tbl->hash.find("__class__");
                        if (it != tbl->hash.end() && it->second.is_string()) {
                            std::string cur = it->second.as_string();
                            while (true) {
                                if (cur == target) { result = true; break; }
                                // Look up the class prototype in globals
                                auto git = globals_.find(cur);
                                if (git == globals_.end() || !git->second.is_table()) break;
                                auto* ct = git->second.as_table();
                                // Check trait membership marker
                                if (ct->get(trait_key).truthy()) { result = true; break; }
                                // Walk base chain
                                auto bit = ct->hash.find("__base__");
                                if (bit == ct->hash.end() || !bit->second.is_table()) break;
                                auto* bt = bit->second.as_table();
                                auto cnit = bt->hash.find("__class__");
                                if (cnit == bt->hash.end() || !cnit->second.is_string()) break;
                                cur = cnit->second.as_string();
                            }
                        }
                    }
                    R(A) = Value::from_bool(result);
                    break;
                }

                case Op::SliceFrom: {
                    // A=dest, B=src_table, C=start_index (8-bit immediate)
                    Value& src = R(B);
                    Value out  = Value::from_table();
                    if (src.is_table()) {
                        auto& arr = src.as_table()->array;
                        size_t start = (size_t)C;
                        for (size_t i = start; i < arr.size(); ++i)
                            out.as_table()->array.push_back(arr[i]);
                    }
                    R(A) = std::move(out);
                    break;
                }

                case Op::Nop:
                    break;

                default:
                    runtime_error("unknown opcode: " + std::to_string((int)op));
            }
        } // end inner while

        // Proto ran to end without a Return — implicit nil return
        if (!frame_changed && !frames_.empty()) {
            uint8_t this_base = frames_.back().base_reg;
            uint8_t nr = frames_.back().num_results;
            frames_.pop_back();
            if (!frames_.empty()) {
                for (uint8_t i = 0; i < nr; ++i)
                    regs_[this_base - 1 + i] = Value::nil();
            }
        }

        } catch (const RuntimeError& e) {
            if (!try_stack_.empty()) {
                auto tf = try_stack_.back();
                try_stack_.pop_back();
                // Unwind call stack back to the frame that installed the handler
                while (frames_.size() > tf.frame_count)
                    frames_.pop_back();
                // Jump to catch block and bind error value
                frames_.back().pc = tf.catch_pc;
                regs_[tf.base_reg + tf.catch_reg] =
                    (thrown_value_.tag != Value::Tag::Nil)
                        ? thrown_value_
                        : Value::from_string(e.message);
                thrown_value_ = Value::nil(); // reset
            } else {
                last_error_ = e;
                return false;
            }
        }
    } // end outer while
    return true;
}

// ===========================================================================
// call_value — call any callable Value from C++
// ===========================================================================
Value VM::call_value(const Value& fn, std::vector<Value> args) {
    if (fn.is_native()) {
        auto results = fn.as_native()->fn(std::move(args));
        return results.empty() ? Value::nil() : results[0];
    }
    if (fn.is_closure()) {
        uint8_t base = 0;
        regs_[base] = fn;
        for (size_t i = 0; i < args.size(); ++i)
            regs_[base + 1 + i] = args[i];
        frames_.clear();
        try {
            call(base, (uint8_t)args.size(), 1);
            run();
            return regs_[base];
        } catch (const RuntimeError& e) {
            last_error_ = e;
            return Value::nil();
        }
    }
    return Value::nil();
}

// ===========================================================================
// execute_module overload — used by hotpatch (takes Chunk directly)
// ===========================================================================
bool VM::execute_module(Chunk& chunk, const std::string& mod_name) {
    if (!chunk.main_proto) {
        last_error_ = {"module '" + mod_name + "' has no compiled code", ""};
        return false;
    }
    auto saved = globals_;
    globals_.clear();
    for (auto& [k, v] : saved) globals_[k] = v;

    frames_.clear();
    frames_.push_back({chunk.main_proto, 0, 0});

    bool ok = false;
    try {
        ok = run();
    } catch (const RuntimeError& e) {
        last_error_ = e;
    }

    // Merge new definitions back into globals (they become the module's exports)
    for (auto& [k, v] : globals_) {
        if (saved.find(k) == saved.end())
            saved[k] = v; // new export
        else
            saved[k] = v; // updated export (hotpatch replaces old value)
    }
    globals_ = std::move(saved);
    return ok;
}

// ===========================================================================
// Hotpatch
// ===========================================================================
bool VM::enable_hotpatch(const std::string& dir) {
    if (!hotpatch_)
        hotpatch_ = std::make_unique<HotpatchManager>(*this);
    return hotpatch_->enable(dir);
}

int VM::poll() {
    if (!hotpatch_) return 0;
    return hotpatch_->apply_pending();
}

} // namespace zscript
