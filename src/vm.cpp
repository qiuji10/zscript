#include "vm.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace zscript {

// ===========================================================================
// Constructor
// ===========================================================================
VM::VM() {
    frames_.reserve(MAX_FRAMES);
}

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
// Standard library
// ===========================================================================
void VM::open_stdlib() {
    // print / log
    register_function("print", [](std::vector<Value> args) -> std::vector<Value> {
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) std::cout << "\t";
            std::cout << args[i].to_string();
        }
        std::cout << "\n";
        return {};
    });
    register_function("log", [](std::vector<Value> args) -> std::vector<Value> {
        for (auto& a : args) std::cout << a.to_string();
        std::cout << "\n";
        return {};
    });

    // tostring
    register_function("tostring", [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {Value::from_string("nil")};
        return {Value::from_string(args[0].to_string())};
    });

    // tonumber
    register_function("tonumber", [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {Value::nil()};
        auto& a = args[0];
        if (a.is_int())   return {a};
        if (a.is_float()) return {a};
        if (a.is_string()) {
            try {
                size_t pos;
                int64_t iv = std::stoll(a.as_string(), &pos);
                if (pos == a.as_string().size()) return {Value::from_int(iv)};
                double dv = std::stod(a.as_string(), &pos);
                if (pos == a.as_string().size()) return {Value::from_float(dv)};
            } catch (...) {}
        }
        return {Value::nil()};
    });

    // type
    register_function("type", [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {Value::from_string("nil")};
        return {Value::from_string(args[0].type_name())};
    });

    // assert
    register_function("assert", [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty() || !args[0].truthy()) {
            std::string msg = (args.size() > 1) ? args[1].to_string() : "assertion failed";
            throw RuntimeError{msg, ""};
        }
        return {};
    });

    // max / min
    register_function("max", [](std::vector<Value> args) -> std::vector<Value> {
        if (args.size() < 2) return {args.empty() ? Value::nil() : args[0]};
        auto& a = args[0]; auto& b = args[1];
        if (a.is_int() && b.is_int())
            return {Value::from_int(a.as_int() > b.as_int() ? a.as_int() : b.as_int())};
        return {Value::from_float(a.to_float() > b.to_float() ? a.to_float() : b.to_float())};
    });
    register_function("min", [](std::vector<Value> args) -> std::vector<Value> {
        if (args.size() < 2) return {args.empty() ? Value::nil() : args[0]};
        auto& a = args[0]; auto& b = args[1];
        if (a.is_int() && b.is_int())
            return {Value::from_int(a.as_int() < b.as_int() ? a.as_int() : b.as_int())};
        return {Value::from_float(a.to_float() < b.to_float() ? a.to_float() : b.to_float())};
    });

    // math table
    Value math_tbl = Value::from_table();
    auto* mt = math_tbl.as_table();
    auto set_fn = [&](const std::string& key, NativeFunction::Fn fn) {
        mt->set(key, Value::from_native("math." + key, std::move(fn)));
    };
    set_fn("floor", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty()) return {Value::nil()};
        return {Value::from_int((int64_t)std::floor(a[0].to_float()))};
    });
    set_fn("ceil", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty()) return {Value::nil()};
        return {Value::from_int((int64_t)std::ceil(a[0].to_float()))};
    });
    set_fn("sqrt", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty()) return {Value::nil()};
        return {Value::from_float(std::sqrt(a[0].to_float()))};
    });
    set_fn("abs", [](std::vector<Value> a) -> std::vector<Value> {
        if (a.empty()) return {Value::nil()};
        if (a[0].is_int()) return {Value::from_int(std::abs(a[0].as_int()))};
        return {Value::from_float(std::abs(a[0].to_float()))};
    });
    globals_["math"] = math_tbl;
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
        // Place callee + args in a fresh register window above current usage
        uint8_t base = 0;
        regs_[base] = fn;
        for (size_t i = 0; i < args.size(); ++i) {
            regs_[base + 1 + i] = args[i];
        }
        frames_.clear();
        // dummy frame so run() can use cur_frame()
        CallFrame dummy;
        dummy.proto    = fn.as_closure()->proto;
        dummy.pc       = 0;
        dummy.base_reg = 0;
        // We'll use call() directly
        try {
            call(base, (uint8_t)args.size(), 1);
            run();
            return regs_[base];
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
        Proto* proto = callee.as_closure()->proto;
        if (frames_.size() >= MAX_FRAMES) {
            runtime_error("stack overflow");
        }
        CallFrame frame;
        frame.proto    = proto;
        frame.pc       = 0;
        frame.base_reg = abs_base + 1; // args start at abs_base+1
        frames_.push_back(frame);
        // The callee's registers start at abs_base+1.
        // Parameters are already in abs_base+1..abs_base+num_args.
        // Fill missing params with nil.
        for (uint8_t i = num_args; i < proto->num_params; ++i) {
            regs_[frame.base_reg + i] = Value::nil();
        }
        return true; // run() will execute the new frame
    }

    runtime_error("attempt to call non-callable value (type: " + callee.type_name() + ")");
    return false;
}

// ===========================================================================
// Main interpreter loop
// ===========================================================================
bool VM::run() {
    while (!frames_.empty()) {
        // Snapshot frame state — re-snapshot whenever the frame stack changes.
        Proto*   proto    = frames_.back().proto;
        uint8_t  base_reg = frames_.back().base_reg;
        size_t&  pc       = frames_.back().pc;

        bool frame_changed = false;

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

                case Op::GetField: {
                    // Encoding: A=dest, upper8(Bx)=obj_reg, lower8(Bx)=name_k
                    uint8_t  obj_r  = (uint8_t)(Bx >> 8);
                    uint8_t  name_k = (uint8_t)(Bx & 0xFF);
                    Value& obj = R(obj_r);
                    if (obj.is_table()) {
                        const std::string& field = K(name_k).as_string();
                        R(A) = obj.as_table()->get(field);
                    } else if (obj.is_nil()) {
                        runtime_error("attempt to index nil value");
                    } else {
                        // For non-table values, check the global type table (Phase 3)
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
                    R(A) = Value::from_closure(proto->protos[Bx]);
                    break;
                }

                case Op::Call: {
                    // A=callee_reg, B=num_args, C=num_results
                    bool pushed_frame = !R(A).is_native() && R(A).is_callable();
                    call(A, B, C);
                    if (pushed_frame) {
                        frame_changed = true; // new frame pushed — break inner loop
                    }
                    break;
                }

                case Op::Return: {
                    // A=first_result_reg, B=count (0 = return nil)
                    Value result = (B > 0) ? R(A) : Value::nil();
                    // callee was at base_reg - 1 in the caller's window.
                    uint8_t this_base = base_reg;
                    frames_.pop_back();
                    frame_changed = true;
                    // Now frame/proto are dangling — do not access them again.
                    if (!frames_.empty()) {
                        regs_[this_base - 1] = result;
                    } else {
                        regs_[0] = result;
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

                case Op::Nop:
                    break;

                default:
                    runtime_error("unknown opcode: " + std::to_string((int)op));
            }
        } // end inner while

        // Proto ran to end without a Return — implicit nil return
        if (!frame_changed && !frames_.empty()) {
            uint8_t this_base = frames_.back().base_reg;
            frames_.pop_back();
            if (!frames_.empty()) {
                regs_[this_base - 1] = Value::nil();
            }
        }
    } // end outer while
    return true;
}

} // namespace zscript
