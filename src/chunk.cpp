#include "chunk.h"
#include "value.h"
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace zscript {

// ---------------------------------------------------------------------------
// Value methods
// ---------------------------------------------------------------------------
bool Value::operator==(const Value& o) const {
    if (tag != o.tag) {
        // int/float cross-comparison
        if (is_number() && o.is_number()) return to_float() == o.to_float();
        return false;
    }
    switch (tag) {
        case Tag::Nil:     return true;
        case Tag::Bool:    return b == o.b;
        case Tag::Int:     return i == o.i;
        case Tag::Float:   return f == o.f;
        case Tag::String:  return str_ptr->data == o.str_ptr->data;
        case Tag::Table:   return table_ptr.get() == o.table_ptr.get();
        case Tag::Closure: return closure_ptr.get() == o.closure_ptr.get();
        case Tag::Native:  return native_ptr.get() == o.native_ptr.get();
    }
    return false;
}

std::string Value::to_string() const {
    switch (tag) {
        case Tag::Nil:     return "nil";
        case Tag::Bool:    return b ? "true" : "false";
        case Tag::Int:     return std::to_string(i);
        case Tag::Float: {
            std::ostringstream ss;
            ss << std::setprecision(14) << f;
            std::string s = ss.str();
            // Ensure there is a decimal point so it reads as float
            if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
                s += ".0";
            return s;
        }
        case Tag::String:  return str_ptr->data;
        case Tag::Table:   return "<table>";
        case Tag::Closure: return "<fn " + closure_ptr->proto->name + ">";
        case Tag::Native:  return "<native " + native_ptr->name + ">";
    }
    return "<?>";
}

std::string Value::type_name() const {
    switch (tag) {
        case Tag::Nil:     return "nil";
        case Tag::Bool:    return "bool";
        case Tag::Int:     return "int";
        case Tag::Float:   return "float";
        case Tag::String:  return "string";
        case Tag::Table:   return "table";
        case Tag::Closure: return "function";
        case Tag::Native:  return "native";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Op names
// ---------------------------------------------------------------------------
const char* op_name(Op op) {
    switch (op) {
        case Op::LoadNil:   return "LOAD_NIL";
        case Op::LoadBool:  return "LOAD_BOOL";
        case Op::LoadInt:   return "LOAD_INT";
        case Op::LoadK:     return "LOAD_K";
        case Op::Move:      return "MOVE";
        case Op::GetGlobal: return "GET_GLOBAL";
        case Op::SetGlobal: return "SET_GLOBAL";
        case Op::GetUpval:  return "GET_UPVAL";
        case Op::SetUpval:  return "SET_UPVAL";
        case Op::NewTable:  return "NEW_TABLE";
        case Op::Inherit:   return "INHERIT";
        case Op::GetField:  return "GET_FIELD";
        case Op::SetField:  return "SET_FIELD";
        case Op::GetIndex:  return "GET_INDEX";
        case Op::SetIndex:  return "SET_INDEX";
        case Op::Add:       return "ADD";
        case Op::Sub:       return "SUB";
        case Op::Mul:       return "MUL";
        case Op::Div:       return "DIV";
        case Op::Mod:       return "MOD";
        case Op::Neg:       return "NEG";
        case Op::Eq:        return "EQ";
        case Op::Ne:        return "NE";
        case Op::Lt:        return "LT";
        case Op::Le:        return "LE";
        case Op::Not:       return "NOT";
        case Op::And:       return "AND";
        case Op::Or:        return "OR";
        case Op::Jump:      return "JUMP";
        case Op::JumpFalse: return "JUMP_FALSE";
        case Op::JumpTrue:  return "JUMP_TRUE";
        case Op::Call:       return "CALL";
        case Op::CallMethod: return "CALL_METHOD";
        case Op::Return:     return "RETURN";
        case Op::Closure:   return "CLOSURE";
        case Op::Concat:    return "CONCAT";
        case Op::CheckNil:  return "CHECK_NIL";
        case Op::Import:    return "IMPORT";
        case Op::TLen:      return "TLEN";
        case Op::Pow:       return "POW";
        case Op::TableKeys: return "TABLE_KEYS";
        case Op::Throw:     return "THROW";
        case Op::PushTry:   return "PUSH_TRY";
        case Op::PopTry:    return "POP_TRY";
        case Op::IsInstance: return "IS_INSTANCE";
        case Op::SliceFrom: return "SLICE_FROM";
        case Op::NewRange:     return "NEW_RANGE";
        case Op::NewRangeExcl: return "NEW_RANGE_EXCL";
        case Op::Nop:       return "NOP";
        default:            return "???";
    }
}

// ---------------------------------------------------------------------------
// Disassembler
// ---------------------------------------------------------------------------
static void print_constant(const Value& v) {
    std::cout << v.to_string();
}

void disassemble_proto(const Proto& proto, int depth) {
    std::string indent(depth * 2, ' ');
    std::cout << indent << "== proto: " << (proto.name.empty() ? "<anon>" : proto.name)
              << " [params=" << (int)proto.num_params
              << " regs=" << (int)proto.max_regs << "] ==\n";

    for (size_t i = 0; i < proto.code.size(); ++i) {
        uint32_t instr = proto.code[i];
        Op op = instr_op(instr);
        uint8_t A  = instr_A(instr);
        uint8_t B  = instr_B(instr);
        uint8_t C  = instr_C(instr);
        uint16_t Bx = instr_Bx(instr);
        int32_t  sbx = instr_sBx(instr);

        uint32_t line = (i < proto.lines.size()) ? proto.lines[i] : 0;
        std::cout << indent << std::setw(4) << i
                  << "  [" << std::setw(4) << line << "]  "
                  << std::left << std::setw(14) << op_name(op);

        switch (op) {
            case Op::LoadNil:
                std::cout << "R[" << (int)A << "]";
                break;
            case Op::LoadBool:
                std::cout << "R[" << (int)A << "] = " << (B ? "true" : "false");
                break;
            case Op::LoadInt:
                std::cout << "R[" << (int)A << "] = " << sbx;
                break;
            case Op::LoadK:
                std::cout << "R[" << (int)A << "] = K[" << Bx << "] (";
                if (Bx < proto.constants.size()) print_constant(proto.constants[Bx]);
                std::cout << ")";
                break;
            case Op::Move:
                std::cout << "R[" << (int)A << "] = R[" << (int)B << "]";
                break;
            case Op::GetGlobal:
            case Op::SetGlobal:
                std::cout << "R[" << (int)A << "] K[" << Bx << "] (";
                if (Bx < proto.constants.size()) print_constant(proto.constants[Bx]);
                std::cout << ")";
                break;
            case Op::Add: case Op::Sub: case Op::Mul:
            case Op::Div: case Op::Mod:
            case Op::Eq:  case Op::Ne:
            case Op::Lt:  case Op::Le:
            case Op::And: case Op::Or:
                std::cout << "R[" << (int)A << "] = R[" << (int)B << "] op R[" << (int)C << "]";
                break;
            case Op::Neg: case Op::Not:
                std::cout << "R[" << (int)A << "] = op R[" << (int)B << "]";
                break;
            case Op::Jump:
                std::cout << "PC += " << sbx << "  (-> " << (int)(i + 1 + sbx) << ")";
                break;
            case Op::JumpFalse: case Op::JumpTrue:
                std::cout << "if R[" << (int)A << "] PC += " << sbx
                          << "  (-> " << (int)(i + 1 + sbx) << ")";
                break;
            case Op::Call:
                std::cout << "R[" << (int)A << "](R[" << (int)A+1 << ".." << (int)A+(int)B << "]) -> R[" << (int)A << ".." << (int)A+(int)C-1 << "]";
                break;
            case Op::Return:
                std::cout << "R[" << (int)A << ".." << (int)A+(int)B-1 << "]";
                break;
            case Op::Closure:
                std::cout << "R[" << (int)A << "] = Proto[" << Bx << "]";
                break;
            case Op::GetField:
                std::cout << "R[" << (int)A << "] = R[" << (int)B << "].K[" << Bx << "]";
                break;
            case Op::SetField:
                std::cout << "R[" << (int)A << "].K[" << (Bx>>8) << "] = R[" << (int)B << "]";
                break;
            case Op::Concat:
                std::cout << "R[" << (int)A << "] = R[" << (int)B << "] .. R[" << (int)C << "]";
                break;
            default:
                std::cout << "A=" << (int)A << " B=" << (int)B << " C=" << (int)C;
                break;
        }
        std::cout << "\n";
    }

    // Nested protos
    for (auto* p : proto.protos) {
        disassemble_proto(*p, depth + 1);
    }
}

void disassemble_chunk(const Chunk& chunk) {
    std::cout << "=== " << chunk.filename << " ===\n";
    if (chunk.main_proto) disassemble_proto(*chunk.main_proto);
}

// ---------------------------------------------------------------------------
// Serialization (little-endian binary)
// ---------------------------------------------------------------------------
struct Writer {
    std::vector<uint8_t> buf;
    void u8(uint8_t v)  { buf.push_back(v); }
    void u16(uint16_t v){ u8(v & 0xFF); u8(v >> 8); }
    void u32(uint32_t v){ u16(v & 0xFFFF); u16(v >> 16); }
    void u64(uint64_t v){ u32(v & 0xFFFFFFFF); u32(v >> 32); }
    void str(const std::string& s) {
        u32((uint32_t)s.size());
        for (char c : s) u8((uint8_t)c);
    }
};

static void write_value(Writer& w, const Value& v) {
    w.u8((uint8_t)v.tag);
    switch (v.tag) {
        case Value::Tag::Nil:   break;
        case Value::Tag::Bool:  w.u8(v.b ? 1 : 0); break;
        case Value::Tag::Int:   w.u64((uint64_t)v.i); break;
        case Value::Tag::Float: {
            uint64_t bits;
            static_assert(sizeof(bits) == sizeof(v.f));
            std::memcpy(&bits, &v.f, sizeof(bits));
            w.u64(bits);
            break;
        }
        case Value::Tag::String: w.str(v.str_ptr->data); break;
        default: break; // functions/tables not serializable in .zbc
    }
}

static void write_proto(Writer& w, const Proto& p) {
    w.str(p.name);
    w.u8(p.num_params);
    w.u8(p.max_regs);
    w.u8(p.is_vararg ? 1 : 0);

    w.u32((uint32_t)p.code.size());
    for (auto instr : p.code) w.u32(instr);

    w.u32((uint32_t)p.constants.size());
    for (auto& v : p.constants) write_value(w, v);

    w.u32((uint32_t)p.lines.size());
    for (auto l : p.lines) w.u32(l);

    w.u32((uint32_t)p.protos.size());
    for (auto* nested : p.protos) write_proto(w, *nested);
}

std::vector<uint8_t> serialize_chunk(const Chunk& chunk) {
    Writer w;
    w.u32(ZBC_MAGIC);
    w.u8(ZBC_VERSION);
    w.str(chunk.filename);
    write_proto(w, *chunk.main_proto);
    return w.buf;
}

// ---------------------------------------------------------------------------
// Deserialization
// ---------------------------------------------------------------------------
struct Reader {
    const uint8_t* data;
    size_t         size;
    size_t         pos = 0;
    bool           ok  = true;

    uint8_t  u8()  { if (pos >= size) { ok=false; return 0; } return data[pos++]; }
    uint16_t u16() { uint16_t v = u8(); v |= ((uint16_t)u8() << 8); return v; }
    uint32_t u32() { uint32_t v = u16(); v |= ((uint32_t)u16() << 16); return v; }
    uint64_t u64() { uint64_t v = u32(); v |= ((uint64_t)u32() << 32); return v; }
    std::string str() {
        uint32_t len = u32();
        if (pos + len > size) { ok = false; return {}; }
        std::string s((char*)(data + pos), len);
        pos += len;
        return s;
    }
};

static Value read_value(Reader& r) {
    auto tag = (Value::Tag)r.u8();
    switch (tag) {
        case Value::Tag::Nil:    return Value::nil();
        case Value::Tag::Bool:   return Value::from_bool(r.u8() != 0);
        case Value::Tag::Int:    return Value::from_int((int64_t)r.u64());
        case Value::Tag::Float: {
            uint64_t bits = r.u64();
            double f;
            std::memcpy(&f, &bits, sizeof(f));
            return Value::from_float(f);
        }
        case Value::Tag::String: return Value::from_string(r.str());
        default:                 return Value::nil();
    }
}

static bool read_proto(Reader& r, Chunk& chunk, Proto& p) {
    p.name       = r.str();
    p.num_params = r.u8();
    p.max_regs   = r.u8();
    p.is_vararg  = r.u8() != 0;

    uint32_t code_count = r.u32();
    p.code.resize(code_count);
    for (auto& instr : p.code) instr = r.u32();

    uint32_t const_count = r.u32();
    p.constants.resize(const_count);
    for (auto& v : p.constants) v = read_value(r);

    uint32_t line_count = r.u32();
    p.lines.resize(line_count);
    for (auto& l : p.lines) l = r.u32();

    uint32_t nested_count = r.u32();
    for (uint32_t i = 0; i < nested_count; ++i) {
        Proto* nested = chunk.new_proto();
        p.protos.push_back(nested);
        if (!read_proto(r, chunk, *nested)) return false;
    }
    return r.ok;
}

bool deserialize_chunk(const uint8_t* data, size_t size, Chunk& out) {
    Reader r{data, size};
    uint32_t magic = r.u32();
    if (magic != ZBC_MAGIC) return false;
    uint8_t version = r.u8();
    if (version != ZBC_VERSION) return false;
    out.filename = r.str();
    out.main_proto = out.new_proto();
    return read_proto(r, out, *out.main_proto);
}

} // namespace zscript
