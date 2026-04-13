#pragma once
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zscript {

// ===========================================================================
// Register-based instruction set
//
// Encoding: fixed 32-bit instruction word
//
//   [ opcode : 8 ][ A : 8 ][ B : 8 ][ C : 8 ]
//   [ opcode : 8 ][ A : 8 ][ Bx    : 16     ]   (sBx = Bx - BIAS)
//
// Registers are local to a CallFrame. The VM supports up to 256 regs.
// Constants are stored in Chunk::constants and addressed by index.
// ===========================================================================

enum class Op : uint8_t {
    // --- Loads ---
    LoadNil,      // A               R[A] = nil
    LoadBool,     // A B             R[A] = (bool)B
    LoadInt,      // A Bx            R[A] = (int64) sBx  (small int, bias encoded)
    LoadK,        // A Bx            R[A] = K[Bx]
    Move,         // A B             R[A] = R[B]

    // --- Globals ---
    GetGlobal,    // A Bx            R[A] = Globals[K[Bx]]
    SetGlobal,    // A Bx            Globals[K[Bx]] = R[A]

    // --- Upvalues ---
    GetUpval,     // A B             R[A] = upvalues[B]
    SetUpval,     // A B             upvalues[B] = R[A]

    // --- Tables ---
    NewTable,     // A               R[A] = {}
    Inherit,      // A B             copy all methods from R[B] (parent) into R[A] (child table)
    GetField,     // A B Bx          R[A] = R[B][K[Bx]]
    SetField,     // A B Bx          R[A][K[Bx]] = R[B]
    GetIndex,     // A B C           R[A] = R[B][R[C]]
    SetIndex,     // A B C           R[A][R[B]] = R[C]

    // --- Arithmetic ---
    Add,          // A B C           R[A] = R[B] + R[C]
    Sub,          // A B C           R[A] = R[B] - R[C]
    Mul,          // A B C           R[A] = R[B] * R[C]
    Div,          // A B C           R[A] = R[B] / R[C]
    Mod,          // A B C           R[A] = R[B] % R[C]
    Neg,          // A B             R[A] = -R[B]

    // --- Comparison (produce bool into R[A]) ---
    Eq,           // A B C           R[A] = (R[B] == R[C])
    Ne,           // A B C           R[A] = (R[B] != R[C])
    Lt,           // A B C           R[A] = (R[B] <  R[C])
    Le,           // A B C           R[A] = (R[B] <= R[C])

    // --- Logic ---
    Not,          // A B             R[A] = !R[B]
    And,          // A B C           R[A] = R[B] && R[C]  (short-circuit handled via Jump)
    Or,           // A B C           R[A] = R[B] || R[C]

    // --- Control flow ---
    Jump,         // sBx             PC += sBx
    JumpFalse,    // A sBx           if !R[A]: PC += sBx   (does not pop)
    JumpTrue,     // A sBx           if  R[A]: PC += sBx

    // --- Functions ---
    // Call: plain call. Callee in R[A], args in R[A+1..A+B], result in R[A].
    Call,         // A B C           R[A] = R[A](R[A+1..A+B])
    // CallMethod: method call. Callee in R[A], self in R[A+1], user-args in R[A+2..A+1+B].
    // For closures: frame base = A+1 (self=R[0], args=R[1..B]).
    // For natives: receives only user-args R[A+2..A+1+B] (self is skipped).
    CallMethod,   // A B C           R[A] = R[A](self=R[A+1], R[A+2..A+1+B])
    Return,       // A B             return R[A..A+B-1]  (B=0 → return nil)
    Closure,      // A Bx            R[A] = closure(Proto[Bx])

    // --- String concatenation ---
    Concat,       // A B C           R[A] = R[B] .. R[C]  (string coerce)

    // --- Null safety ---
    CheckNil,     // A Bx            if R[A]==nil: error("force unwrap on nil")   Bx=const name
    // Safe-call helpers: compiler emits JumpFalse around method call

    // --- Engine conditional (no-op after strip) ---
    // Compiler strips @unity / @unreal blocks at compile time based on engine mode.
    // No runtime opcode needed.

    // --- Modules ---
    // Import: load module by name (K[Bx]), store exports table into R[A]
    Import,       // A Bx            R[A] = load_module(K[Bx])

    // --- Table length ---
    TLen,         // A B             R[A] = #R[B]  (array length of table, or string length)

    // --- Power ---
    Pow,          // A B C           R[A] = R[B] ** R[C]

    // --- Table keys (for k,v iteration) ---
    TableKeys,    // A B             R[A] = array of string keys of R[B] (hash part)

    // --- Error handling ---
    Throw,        // A               throw R[A]  (stores value, raises RuntimeError)
    PushTry,      // A sBx           A=catch_reg, sBx=offset to catch block; push try frame
    PopTry,       //                 pop try frame (end of try block)

    // --- Type check ---
    IsInstance,        // A B C   R[A] = (R[B] is instance of class named K[C])
    IsInstanceDynamic, // A B C   R[A] = (R[B] is instance of class in R[C] — runtime value)

    // --- Destructuring rest element ---
    // SliceFrom A, B, C: R[A] = array slice of R[B] starting at index C (8-bit immediate)
    SliceFrom,    // A B C           R[A] = R[B].array[C..]

    // --- Range construction ---
    NewRange,         // A B C           R[A] = R[B]..R[C]   (inclusive)
    NewRangeExcl,     // A B C           R[A] = R[B]..<R[C]  (exclusive)

    // --- Named-arg calls ---
    // CallNamed A B C: like Call but R[A+B+1] is a named-args table {name:val,...}
    CallNamed,        // A B C           R[A](positionals R[A+1..A+B], named R[A+B+1])
    // CallMethodNamed A B C: like CallMethod but R[A+B+2] is a named-args table
    CallMethodNamed,  // A B C           R[A](self=R[A+1], pos R[A+2..A+B+1], named R[A+B+2])

    // --- Delegate ---
    // DelegateAdd A B C: R[A] = delegate_add(R[B], R[C])
    //   If R[C] is callable: build/extend delegate.  Otherwise: R[A] = R[B] + R[C] (arithmetic).
    DelegateAdd,   // A B C
    // DelegateSub A B C: R[A] = delegate_remove(R[B], R[C])
    //   If R[B] is delegate: remove matching handler.  Otherwise: R[A] = R[B] - R[C].
    DelegateSub,   // A B C

    // --- Misc ---
    Nop,

    COUNT
};

const char* op_name(Op op);

// ---------------------------------------------------------------------------
// Instruction encoding helpers
// ---------------------------------------------------------------------------

// Bias for sBx field (16-bit): stored as Bx = sBx + BIAS
static constexpr int32_t BIAS_SBX = 32767;

inline uint32_t encode_ABC(Op op, uint8_t A, uint8_t B, uint8_t C) {
    return (uint32_t(op) & 0xFF)
         | (uint32_t(A)  << 8)
         | (uint32_t(B)  << 16)
         | (uint32_t(C)  << 24);
}

inline uint32_t encode_ABx(Op op, uint8_t A, uint16_t Bx) {
    return (uint32_t(op) & 0xFF)
         | (uint32_t(A)  << 8)
         | (uint32_t(Bx) << 16);
}

inline uint32_t encode_AsBx(Op op, uint8_t A, int32_t sBx) {
    return encode_ABx(op, A, (uint16_t)(sBx + BIAS_SBX));
}

inline uint32_t encode_sBx(Op op, int32_t sBx) {
    return encode_ABx(op, 0, (uint16_t)(sBx + BIAS_SBX));
}

inline Op    instr_op(uint32_t i)  { return Op(i & 0xFF); }
inline uint8_t  instr_A(uint32_t i)  { return (i >> 8)  & 0xFF; }
inline uint8_t  instr_B(uint32_t i)  { return (i >> 16) & 0xFF; }
inline uint8_t  instr_C(uint32_t i)  { return (i >> 24) & 0xFF; }
inline uint16_t instr_Bx(uint32_t i) { return (i >> 16) & 0xFFFF; }
inline int32_t  instr_sBx(uint32_t i){ return int32_t(instr_Bx(i)) - BIAS_SBX; }

// ---------------------------------------------------------------------------
// Prototype — compiled representation of one function / module body
// ---------------------------------------------------------------------------
struct Value; // forward — defined in value.h

// Upvalue descriptor: how to capture a variable when creating a closure.
struct UpvalDesc {
    std::string name;
    bool        is_local = true; // true = from enclosing fn's register; false = from enclosing closure's upvalue
    uint8_t     idx      = 0;   // register index (is_local) or upvalue index (!is_local)
};

struct Proto {
    std::string name;         // debug name
    uint8_t     num_params = 0;
    uint8_t     max_regs   = 0;   // max registers used (set by compiler)
    bool        is_vararg  = false;

    std::vector<uint32_t>    code;       // instructions
    std::vector<Value>       constants;  // constant pool (Value defined in value.h)
    std::vector<Proto*>      protos;     // nested function prototypes (owned by Chunk)
    std::vector<UpvalDesc>   upvalues;   // upvalue descriptors (populated by compiler)
    std::vector<std::string> param_names; // parameter names in order (for named-arg calls)

    // Debug info
    std::vector<uint32_t>    lines;      // code[i] was generated from line lines[i]
    std::vector<std::string> local_names; // name of each register slot (debug)
};

// ---------------------------------------------------------------------------
// Compiled annotation — immutable metadata attached to a class declaration.
// Stored in Chunk (never on the VM heap) so scripts cannot tamper with it.
// ---------------------------------------------------------------------------
struct CompiledAnnotation {
    std::string ns;    // e.g. "unity", "unreal", "scenarioA"
    std::string name;  // e.g. "component", "uclass" (empty for bare @ns)
};

// ---------------------------------------------------------------------------
// Chunk — the top-level compilation unit (one source file)
// ---------------------------------------------------------------------------
struct Chunk {
    std::string filename;
    Proto*      main_proto = nullptr;

    // Owns all Proto objects produced during compilation.
    std::vector<std::unique_ptr<Proto>> all_protos;

    // Annotation metadata: class name → list of @ns.name decorators.
    // Populated at compile time; not accessible from script code.
    std::unordered_map<std::string, std::vector<CompiledAnnotation>> annotations;

    Proto* new_proto(const std::string& name = "") {
        auto p = std::make_unique<Proto>();
        p->name = name;
        Proto* raw = p.get();
        all_protos.push_back(std::move(p));
        return raw;
    }
};

// ---------------------------------------------------------------------------
// Tag set — controls @tag conditional block stripping at compile time.
// The host populates this before compiling; a block is emitted only when
// its tag name is present in the set.  An empty set strips all tag blocks.
//
// Tag names must be valid identifiers: [A-Za-z_][A-Za-z0-9_]*
// IMPORTANT: tags are a compile-time optimization, NOT a security boundary.
// Never use tags as access-control gates for sensitive operations.
// ---------------------------------------------------------------------------
using TagSet = std::unordered_set<std::string>;

// Returns true iff 's' is a valid tag name (same rules as a ZScript identifier).
// Call this before inserting any host-supplied or user-supplied string into a TagSet.
inline bool is_valid_tag(const std::string& s) {
    if (s.empty()) return false;
    if (!std::isalpha((unsigned char)s[0]) && s[0] != '_') return false;
    for (char c : s)
        if (!std::isalnum((unsigned char)c) && c != '_') return false;
    return true;
}

// ---------------------------------------------------------------------------
// .zbc serialization (magic + version + proto tree)
// ---------------------------------------------------------------------------
static constexpr uint32_t ZBC_MAGIC   = 0x7A626300; // "zbc\0"
static constexpr uint8_t  ZBC_VERSION = 1;

// Serialize chunk to bytes
std::vector<uint8_t> serialize_chunk(const Chunk& chunk);

// Deserialize chunk from bytes (returns false on format error)
bool deserialize_chunk(const uint8_t* data, size_t size, Chunk& out);

// Human-readable disassembly
void disassemble_chunk(const Chunk& chunk);
void disassemble_proto(const Proto& proto, int depth = 0);

} // namespace zscript
