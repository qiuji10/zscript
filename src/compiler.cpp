#include "compiler.h"
#include <cassert>
#include <stdexcept>

namespace zscript {

// ===========================================================================
// Constructor
// ===========================================================================
Compiler::Compiler(EngineMode engine) : engine_(engine) {}

// ===========================================================================
// Public entry
// ===========================================================================
std::unique_ptr<Chunk> Compiler::compile(const Program& prog, const std::string& filename) {
    chunk_ = std::make_unique<Chunk>();
    chunk_->filename = filename;

    Proto* main = chunk_->new_proto("<main>");
    chunk_->main_proto = main;

    FnState fs;
    fs.proto = main;
    cur_fn_ = &fs;

    compile_top_level(prog);

    // Ensure a trailing Return
    if (fs.proto->code.empty() ||
        instr_op(fs.proto->code.back()) != Op::Return) {
        emit_ABx(Op::Return, 0, 0);
    }

    fs.proto->max_regs = fs.max_reg;
    cur_fn_ = nullptr;
    return std::move(chunk_);
}

// ===========================================================================
// Register allocation
// ===========================================================================
uint8_t Compiler::alloc_reg() {
    uint8_t r = cur_fn_->next_reg++;
    if (cur_fn_->next_reg > cur_fn_->max_reg)
        cur_fn_->max_reg = cur_fn_->next_reg;
    return r;
}

void Compiler::free_reg(uint8_t r) {
    // Only free if it's the top-most register (prevents issues with out-of-order frees)
    if (r + 1 == cur_fn_->next_reg) cur_fn_->next_reg = r;
}

uint8_t Compiler::cur_reg() const { return cur_fn_->next_reg; }

// ===========================================================================
// Scope
// ===========================================================================
void Compiler::push_scope() { ++cur_fn_->scope; }

void Compiler::pop_scope() {
    int depth = cur_fn_->scope--;
    // Pop locals from this scope
    auto& locals = cur_fn_->locals;
    while (!locals.empty() && locals.back().scope_depth == depth) {
        free_reg(locals.back().reg);
        locals.pop_back();
    }
}

// ===========================================================================
// Locals
// ===========================================================================
void Compiler::define_local(const std::string& name, uint8_t reg, bool is_let) {
    cur_fn_->locals.push_back({name, reg, is_let, cur_fn_->scope});
}

int Compiler::resolve_local(const std::string& name) const {
    auto& locals = cur_fn_->locals;
    for (int i = (int)locals.size() - 1; i >= 0; --i) {
        if (locals[i].name == name) return locals[i].reg;
    }
    return -1;
}

bool Compiler::local_is_let(const std::string& name) const {
    auto& locals = cur_fn_->locals;
    for (int i = (int)locals.size() - 1; i >= 0; --i) {
        if (locals[i].name == name) return locals[i].is_let;
    }
    return false;
}

int Compiler::resolve_upvalue(FnState* fn, const std::string& name) {
    if (!fn->enclosing) return -1;
    // Is it a local in the immediately enclosing function?
    auto& enc_locals = fn->enclosing->locals;
    for (int i = (int)enc_locals.size() - 1; i >= 0; --i) {
        if (enc_locals[i].name == name)
            return fn->add_upvalue(name, /*is_local=*/true, enc_locals[i].reg);
    }
    // Otherwise, is it an upvalue of the enclosing function?
    int upval = resolve_upvalue(fn->enclosing, name);
    if (upval >= 0)
        return fn->add_upvalue(name, /*is_local=*/false, (uint8_t)upval);
    return -1;
}

// ===========================================================================
// Constants
// ===========================================================================
uint16_t Compiler::add_constant(Value v) {
    auto& consts = cur_fn_->proto->constants;
    // Deduplicate
    for (size_t i = 0; i < consts.size(); ++i) {
        if (consts[i] == v) return (uint16_t)i;
    }
    consts.push_back(std::move(v));
    return (uint16_t)(consts.size() - 1);
}

uint16_t Compiler::str_const(const std::string& s) {
    return add_constant(Value::from_string(s));
}

// ===========================================================================
// Instruction emission
// ===========================================================================
size_t Compiler::emit(uint32_t instr, uint32_t line) {
    auto& proto = *cur_fn_->proto;
    proto.code.push_back(instr);
    proto.lines.push_back(line);
    return proto.code.size() - 1;
}

size_t Compiler::emit_ABC(Op op, uint8_t A, uint8_t B, uint8_t C, uint32_t line) {
    return emit(encode_ABC(op, A, B, C), line);
}
size_t Compiler::emit_ABx(Op op, uint8_t A, uint16_t Bx, uint32_t line) {
    return emit(encode_ABx(op, A, Bx), line);
}
size_t Compiler::emit_AsBx(Op op, uint8_t A, int32_t sBx, uint32_t line) {
    return emit(encode_AsBx(op, A, sBx), line);
}
size_t Compiler::emit_sBx(Op op, int32_t sBx, uint32_t line) {
    return emit(encode_sBx(op, sBx), line);
}

size_t Compiler::emit_jump(Op op, uint8_t cond_reg, uint32_t line) {
    return emit_AsBx(op, cond_reg, 0, line); // sBx=0 is a placeholder
}

void Compiler::patch_jump(size_t idx) {
    auto& code = cur_fn_->proto->code;
    int offset = (int)code.size() - (int)idx - 1;
    // Re-encode with the correct sBx
    uint32_t orig = code[idx];
    Op op  = instr_op(orig);
    uint8_t A  = instr_A(orig);
    code[idx] = encode_AsBx(op, A, offset);
}

int Compiler::current_pc() const {
    return (int)cur_fn_->proto->code.size();
}

// ===========================================================================
// Error
// ===========================================================================
void Compiler::error(const SourceLoc& loc, const std::string& msg) {
    errors_.push_back({loc, msg});
}

// ===========================================================================
// Compile helper: move result into dest if requested
// ===========================================================================
uint8_t Compiler::into(uint8_t result_reg, std::optional<uint8_t> dest) {
    if (!dest || *dest == result_reg) return result_reg;
    emit_ABC(Op::Move, *dest, result_reg, 0);
    return *dest;
}

// ===========================================================================
// Top-level compilation
// ===========================================================================
void Compiler::compile_top_level(const Program& prog) {
    for (auto& decl : prog.decls) {
        if (auto* fn = dynamic_cast<const FnDecl*>(decl.get())) {
            // Define global function: register it as a closure in a global slot
            uint8_t reg = alloc_reg();
            compile_fn_decl(*fn, reg, fn->loc.line);
            uint16_t name_k = str_const(fn->name);
            emit_ABx(Op::SetGlobal, reg, name_k, fn->loc.line);
            free_reg(reg);
        } else if (auto* cls = dynamic_cast<const ClassDecl*>(decl.get())) {
            compile_class_decl(*cls, cls->loc.line);
        } else if (auto* en = dynamic_cast<const EnumDecl*>(decl.get())) {
            compile_enum_decl(*en, en->loc.line);
        } else if (auto* fd = dynamic_cast<const FieldDecl*>(decl.get())) {
            // Top-level var/let → global
            if (fd->init) {
                uint8_t reg = alloc_reg();
                compile_expr(*fd->init, reg);
                uint16_t name_k = str_const(fd->name);
                emit_ABx(Op::SetGlobal, reg, name_k, fd->loc.line);
                free_reg(reg);
            }
        } else if (auto* sd = dynamic_cast<const StmtDecl*>(decl.get())) {
            compile_stmt(*sd->stmt);
        } else if (auto* imp = dynamic_cast<const ImportDecl*>(decl.get())) {
            // import module.name [as alias]
            // Emits: R[reg] = load_module(path); Globals[alias] = R[reg]
            uint8_t reg = alloc_reg();
            uint16_t path_k = str_const(imp->path);
            emit_ABx(Op::Import, reg, path_k, imp->loc.line);
            uint16_t alias_k = str_const(imp->alias);
            emit_ABx(Op::SetGlobal, reg, alias_k, imp->loc.line);
            free_reg(reg);
        }
        // Trait/Impl: Phase 3
    }
}

// ===========================================================================
// Compile a function declaration → Closure stored in dest_reg
// ===========================================================================
void Compiler::compile_fn_decl(const FnDecl& fn, uint8_t dest_reg, uint32_t line) {
    Proto* fn_proto = chunk_->new_proto(fn.name);
    fn_proto->num_params = (uint8_t)fn.params.size();
    if (!fn.params.empty() && fn.params.back().is_vararg)
        fn_proto->is_vararg = true;

    // New FnState
    FnState child_fs;
    child_fs.proto     = fn_proto;
    child_fs.enclosing = cur_fn_;
    FnState* prev = cur_fn_;
    cur_fn_ = &child_fs;

    push_scope();
    // Parameters occupy the first registers
    std::vector<uint8_t> param_regs;
    for (auto& param : fn.params) {
        uint8_t r = alloc_reg();
        define_local(param.name, r, !param.is_mut);
        param_regs.push_back(r);
    }
    // Emit default-value guards: if param is nil, assign the default
    for (size_t i = 0; i < fn.params.size(); ++i) {
        const auto& param = fn.params[i];
        if (!param.default_val || param.is_vararg) continue;
        uint8_t r   = param_regs[i];
        uint8_t cmp = alloc_reg();
        emit_ABx(Op::LoadNil, cmp, 0, param.loc.line);
        emit_ABC(Op::Eq, cmp, r, cmp, param.loc.line);  // cmp = (arg == nil)
        size_t jmp = emit_jump(Op::JumpFalse, cmp, param.loc.line); // skip if provided
        free_reg(cmp);
        compile_expr(*param.default_val, r);
        patch_jump(jmp);
    }

    compile_block(fn.body);

    // Implicit nil return if no explicit return
    if (fn_proto->code.empty() ||
        instr_op(fn_proto->code.back()) != Op::Return) {
        emit_ABx(Op::Return, 0, 0);
    }

    pop_scope();
    fn_proto->max_regs = child_fs.max_reg;
    cur_fn_ = prev;

    // Register nested proto
    size_t proto_idx = 0;
    for (size_t i = 0; i < chunk_->all_protos.size(); ++i) {
        if (chunk_->all_protos[i].get() == fn_proto) {
            proto_idx = i;
            break;
        }
    }
    // Store in parent proto's nested list
    cur_fn_->proto->protos.push_back(fn_proto);
    uint16_t nested_idx = (uint16_t)(cur_fn_->proto->protos.size() - 1);

    emit_ABx(Op::Closure, dest_reg, nested_idx, line);
    for (auto& uv : fn_proto->upvalues) {
        if (uv.is_local)
            emit_ABC(Op::Move,     0, uv.idx, 0, line);
        else
            emit_ABC(Op::GetUpval, 0, uv.idx, 0, line);
    }
}

// ===========================================================================
// Class declaration → prototype table stored as global.
//
// Each method gets an implicit `self` parameter injected as register 0.
// The class table also gets a `__methods__` marker and a synthetic `new`
// function that:
//   1. Creates a fresh instance table
//   2. Copies all methods from the prototype
//   3. Calls `init(self, args...)` if it exists
//   4. Returns self
//
// Method call convention is handled in compile_call: when the callee is
// `obj.method`, we insert obj as the first argument (self).
// ===========================================================================
void Compiler::compile_class_decl(const ClassDecl& cls, uint32_t line) {
    // Register class name so compile_call knows Foo() means instantiation.
    // Only non-static methods count as instance methods.
    std::vector<std::string> method_names;
    for (auto& m : cls.members)
        if (auto* fn = dynamic_cast<const FnDecl*>(m.get()))
            if (!fn->is_static)
                method_names.push_back(fn->name);
    class_methods_[cls.name] = method_names;

    uint8_t tbl_reg = alloc_reg();
    emit_ABC(Op::NewTable, tbl_reg, 0, 0, line);

    // Inheritance: copy base class methods into this prototype, then own methods override.
    if (cls.base) {
        uint8_t base_reg = alloc_reg();
        uint16_t base_name_k = str_const(*cls.base);
        emit_ABx(Op::GetGlobal, base_reg, base_name_k, line);
        emit_ABC(Op::Inherit, tbl_reg, base_reg, 0, line);
        // Store __base__ so super calls can find the parent prototype.
        uint16_t base_key_k = str_const("__base__");
        emit_ABx(Op::SetField, tbl_reg,
                 (uint16_t)((base_key_k << 8) | base_reg), line);
        free_reg(base_reg);
    }

    // Collect static member names for the __statics__ marker.
    std::vector<std::string> static_names;

    // Compile each member.
    for (auto& member : cls.members) {
        // ── Instance/static field (FieldDecl) ──
        if (auto* fd = dynamic_cast<const FieldDecl*>(member.get())) {
            if (fd->init) {
                uint8_t val_reg = alloc_reg();
                compile_expr(*fd->init, val_reg);
                uint16_t name_k = str_const(fd->name);
                emit_ABx(Op::SetField, tbl_reg,
                         (uint16_t)((name_k << 8) | val_reg), fd->loc.line);
                free_reg(val_reg);
            }
            if (fd->is_static) static_names.push_back(fd->name);
            continue;
        }

        // ── Method (FnDecl) ──
        auto* fn = dynamic_cast<const FnDecl*>(member.get());
        if (!fn) continue;

        Proto* fn_proto = chunk_->new_proto(cls.name + "." + fn->name);

        FnState child_fs;
        child_fs.proto     = fn_proto;
        child_fs.enclosing = cur_fn_;
        FnState* prev  = cur_fn_;
        cur_fn_        = &child_fs;
        current_class_      = fn->is_static ? "" : cls.name;
        current_base_class_ = fn->is_static ? "" : cls.base.value_or("");

        push_scope();
        std::vector<uint8_t> method_param_regs;

        if (fn->is_static) {
            // Static method: no self param — behaves like a regular function
            fn_proto->num_params = (uint8_t)fn->params.size();
            for (auto& param : fn->params) {
                uint8_t r = alloc_reg();
                define_local(param.name, r, !param.is_mut);
                method_param_regs.push_back(r);
            }
            static_names.push_back(fn->name);
        } else {
            // Instance method: self is register 0
            fn_proto->num_params = (uint8_t)(fn->params.size() + 1);
            uint8_t self_reg = alloc_reg();
            define_local("self", self_reg, /*is_let=*/false);
            for (auto& param : fn->params) {
                uint8_t r = alloc_reg();
                define_local(param.name, r, !param.is_mut);
                method_param_regs.push_back(r);
            }
        }

        // Default-value guards
        for (size_t i = 0; i < fn->params.size(); ++i) {
            const auto& param = fn->params[i];
            if (!param.default_val || param.is_vararg) continue;
            uint8_t r   = method_param_regs[i];
            uint8_t cmp = alloc_reg();
            emit_ABx(Op::LoadNil, cmp, 0, param.loc.line);
            emit_ABC(Op::Eq, cmp, r, cmp, param.loc.line);
            size_t jmp = emit_jump(Op::JumpFalse, cmp, param.loc.line);
            free_reg(cmp);
            compile_expr(*param.default_val, r);
            patch_jump(jmp);
        }
        compile_block(fn->body);
        if (fn_proto->code.empty() ||
            instr_op(fn_proto->code.back()) != Op::Return)
            emit_ABx(Op::Return, 0, 0);
        pop_scope();

        fn_proto->max_regs = child_fs.max_reg;
        cur_fn_             = prev;
        current_class_      = "";
        current_base_class_ = "";

        // Register in parent proto's nested list and emit Closure
        cur_fn_->proto->protos.push_back(fn_proto);
        uint16_t nested_idx = (uint16_t)(cur_fn_->proto->protos.size() - 1);
        uint8_t fn_reg = alloc_reg();
        emit_ABx(Op::Closure, fn_reg, nested_idx, fn->loc.line);

        // Store into prototype table
        uint16_t name_k = str_const(fn->name);
        emit_ABx(Op::SetField, tbl_reg,
                 (uint16_t)((name_k << 8) | fn_reg), fn->loc.line);
        free_reg(fn_reg);
    }

    // Emit __statics__ table so the VM can skip static members during instantiation.
    if (!static_names.empty()) {
        uint8_t statics_tbl = alloc_reg();
        emit_ABC(Op::NewTable, statics_tbl, 0, 0, line);
        for (auto& sname : static_names) {
            uint8_t bool_reg = alloc_reg();
            emit_ABx(Op::LoadBool, bool_reg, 1, line); // true
            uint16_t name_k = str_const(sname);
            emit_ABx(Op::SetField, statics_tbl,
                     (uint16_t)((name_k << 8) | bool_reg), line);
            free_reg(bool_reg);
        }
        uint16_t statics_key = str_const("__statics__");
        emit_ABx(Op::SetField, tbl_reg,
                 (uint16_t)((statics_key << 8) | statics_tbl), line);
        free_reg(statics_tbl);
    }

    // Mark table as a class prototype so the VM / runtime can identify it.
    {
        uint8_t mark_reg = alloc_reg();
        uint16_t mk = str_const(cls.name);
        emit_ABx(Op::LoadK, mark_reg, mk, line);
        uint16_t key_k = str_const("__class__");
        emit_ABx(Op::SetField, tbl_reg,
                 (uint16_t)((key_k << 8) | mark_reg), line);
        free_reg(mark_reg);
    }

    uint16_t cls_name_k = str_const(cls.name);
    emit_ABx(Op::SetGlobal, tbl_reg, cls_name_k, line);
    free_reg(tbl_reg);
}

// ===========================================================================
// Enum declaration — compiles to a frozen table stored as a global.
//   enum Direction { North, South, East, West }
//   → Direction = {North:0, South:1, East:2, West:3}
// ===========================================================================
void Compiler::compile_enum_decl(const EnumDecl& e, uint32_t line) {
    uint8_t tbl_reg = alloc_reg();
    emit_ABC(Op::NewTable, tbl_reg, 0, 0, line);

    for (auto& v : e.variants) {
        uint8_t val_reg = alloc_reg();
        int64_t iv = v.value.value_or(0);
        uint16_t k = add_constant(Value::from_int(iv));
        emit_ABx(Op::LoadK, val_reg, k, line);
        uint16_t name_k = str_const(v.name);
        emit_ABx(Op::SetField, tbl_reg,
                 (uint16_t)((name_k << 8) | val_reg), line);
        free_reg(val_reg);
    }

    // Also store the enum name in __enum__ so runtime/debug can identify it.
    {
        uint8_t mark_reg = alloc_reg();
        uint16_t mk = str_const(e.name);
        emit_ABx(Op::LoadK, mark_reg, mk, line);
        uint16_t key_k = str_const("__enum__");
        emit_ABx(Op::SetField, tbl_reg,
                 (uint16_t)((key_k << 8) | mark_reg), line);
        free_reg(mark_reg);
    }

    uint16_t enum_name_k = str_const(e.name);
    emit_ABx(Op::SetGlobal, tbl_reg, enum_name_k, line);
    free_reg(tbl_reg);
}

// ===========================================================================
// Statements
// ===========================================================================
void Compiler::compile_block(const Block& block) {
    push_scope();
    for (auto& stmt : block.stmts) {
        compile_stmt(*stmt);
    }
    pop_scope();
}

void Compiler::compile_stmt(const Stmt& stmt) {
    if (auto* s = dynamic_cast<const VarDeclStmt*>(&stmt)) {
        compile_var_decl(*s);
    } else if (auto* s = dynamic_cast<const ReturnStmt*>(&stmt)) {
        compile_return(*s);
    } else if (auto* s = dynamic_cast<const IfStmt*>(&stmt)) {
        compile_if(*s);
    } else if (auto* s = dynamic_cast<const WhileStmt*>(&stmt)) {
        compile_while(*s);
    } else if (auto* s = dynamic_cast<const ForStmt*>(&stmt)) {
        compile_for(*s);
    } else if (auto* s = dynamic_cast<const EngineBlock*>(&stmt)) {
        compile_engine_block(*s);
    } else if (auto* s = dynamic_cast<const MatchStmt*>(&stmt)) {
        compile_match(*s);
    } else if (auto* s = dynamic_cast<const ThrowStmt*>(&stmt)) {
        compile_throw(*s);
    } else if (auto* s = dynamic_cast<const TryCatchStmt*>(&stmt)) {
        compile_try_catch(*s);
    } else if (auto* s = dynamic_cast<const BlockStmt*>(&stmt)) {
        compile_block(s->block);
    } else if (dynamic_cast<const BreakStmt*>(&stmt)) {
        if (cur_fn_->break_patches.empty()) {
            error(stmt.loc, "'break' outside of loop");
        } else {
            size_t idx = emit_sBx(Op::Jump, 0, stmt.loc.line);
            cur_fn_->break_patches.back().push_back({idx});
        }
    } else if (dynamic_cast<const ContinueStmt*>(&stmt)) {
        if (cur_fn_->continue_targets.empty()) {
            error(stmt.loc, "'continue' outside of loop");
        } else {
            int target = cur_fn_->continue_targets.back();
            if (target >= 0) {
                // Target is known (while loop) — emit direct jump
                int offset = target - (int)current_pc() - 1;
                emit_sBx(Op::Jump, offset, stmt.loc.line);
            } else {
                // Target not yet known (for loop increment) — use patch list
                size_t idx = emit_sBx(Op::Jump, 0, stmt.loc.line);
                cur_fn_->continue_patches.back().push_back({idx});
            }
        }
    } else if (auto* s = dynamic_cast<const MultiVarDeclStmt*>(&stmt)) {
        compile_multi_var_decl(*s);
    } else if (auto* s = dynamic_cast<const DestructureStmt*>(&stmt)) {
        compile_destructure(*s);
    } else if (auto* s = dynamic_cast<const ExprStmt*>(&stmt)) {
        uint8_t r = alloc_reg();
        compile_expr(*s->expr, r);
        free_reg(r);
    }
}

void Compiler::compile_multi_var_decl(const MultiVarDeclStmt& s) {
    uint8_t N = (uint8_t)s.names.size();
    uint8_t first_reg = cur_reg();

    // Compile the init expression (expected to be a call) into first_reg.
    compile_expr(*s.init, first_reg);

    // Patch the last Call/CallMethod instruction to expect N results instead of 1.
    auto& code = cur_fn_->proto->code;
    if (!code.empty()) {
        Op last_op = instr_op(code.back());
        if (last_op == Op::Call || last_op == Op::CallMethod) {
            uint32_t instr = code.back();
            code.back() = encode_ABC(last_op, instr_A(instr), instr_B(instr), N);
        }
    }

    // Results land in first_reg..first_reg+N-1; update register watermark.
    cur_fn_->next_reg = first_reg + N;
    if (cur_fn_->max_reg < cur_fn_->next_reg)
        cur_fn_->max_reg = cur_fn_->next_reg;

    // Define each name as a local backed by its result register.
    for (uint8_t i = 0; i < N; ++i)
        define_local(s.names[i], first_reg + i, s.is_let[i]);
}

void Compiler::compile_destructure(const DestructureStmt& s) {
    // Compile the RHS into a temp register.
    uint8_t src = alloc_reg();
    compile_expr(*s.init, src);

    uint32_t line = s.loc.line;

    auto bind = [&](const std::string& name, uint8_t val_reg) {
        if (s.is_global) {
            uint16_t k = str_const(name);
            emit_ABx(Op::SetGlobal, val_reg, k, line);
            free_reg(val_reg);
        } else {
            define_local(name, val_reg, s.is_let);
        }
    };

    if (s.kind == DestructureStmt::Kind::Array) {
        int64_t idx = 0;
        for (auto& b : s.bindings) {
            uint8_t dest = alloc_reg();
            if (b.is_rest) {
                // SliceFrom dest, src, idx  → dest = src.array[idx..]
                emit_ABC(Op::SliceFrom, dest, src, (uint8_t)idx, line);
            } else {
                uint8_t idx_reg = alloc_reg();
                emit_AsBx(Op::LoadInt, idx_reg, idx, line);
                emit_ABC(Op::GetIndex, dest, src, idx_reg, line);
                free_reg(idx_reg);
                ++idx;
            }
            bind(b.name, dest);
        }
    } else {
        // Table destructuring
        for (auto& b : s.bindings) {
            uint8_t dest    = alloc_reg();
            const std::string& field = b.key.empty() ? b.name : b.key;
            uint16_t key_k  = str_const(field);
            // GetField encoding: A=dest, upper8(Bx)=obj_reg, lower8(Bx)=name_k
            emit_ABx(Op::GetField, dest, (uint16_t)((src << 8) | (key_k & 0xFF)), line);
            bind(b.name, dest);
        }
    }

    free_reg(src);
}

void Compiler::compile_var_decl(const VarDeclStmt& s) {
    uint8_t reg = alloc_reg();
    if (s.init) {
        compile_expr(*s.init, reg);
    } else {
        emit_ABx(Op::LoadNil, reg, 0, s.loc.line);
    }
    define_local(s.name, reg, s.is_let);
}

void Compiler::compile_return(const ReturnStmt& s) {
    if (s.values.empty()) {
        emit_ABx(Op::Return, 0, 0, s.loc.line);
    } else if (s.values.size() == 1) {
        uint8_t reg = alloc_reg();
        compile_expr(*s.values[0], reg);
        emit_ABx(Op::Return, reg, 1, s.loc.line);
        free_reg(reg);
    } else {
        // Multi-return: emit all values into consecutive registers.
        uint8_t first = cur_reg();
        for (size_t i = 0; i < s.values.size(); ++i) {
            uint8_t r = alloc_reg();
            compile_expr(*s.values[i], r);
        }
        emit_ABx(Op::Return, first, (uint16_t)s.values.size(), s.loc.line);
        cur_fn_->next_reg = first; // regs are gone after return
    }
}

void Compiler::compile_if(const IfStmt& s) {
    uint8_t cond_reg = alloc_reg();
    compile_expr(*s.cond, cond_reg);

    size_t jump_false_idx = emit_jump(Op::JumpFalse, cond_reg, s.loc.line);
    free_reg(cond_reg);

    compile_block(s.then_block);

    if (s.else_clause) {
        size_t jump_over_else = emit_sBx(Op::Jump, 0, s.loc.line);
        patch_jump(jump_false_idx);
        compile_stmt(*s.else_clause);
        patch_jump(jump_over_else);
    } else {
        patch_jump(jump_false_idx);
    }
}

void Compiler::compile_while(const WhileStmt& s) {
    int loop_start = current_pc();

    // Push loop context for break/continue
    cur_fn_->break_patches.push_back({});
    cur_fn_->continue_patches.push_back({});
    cur_fn_->continue_targets.push_back(loop_start);

    uint8_t cond_reg = alloc_reg();
    compile_expr(*s.cond, cond_reg);
    size_t exit_jump = emit_jump(Op::JumpFalse, cond_reg, s.loc.line);
    free_reg(cond_reg);

    compile_block(s.body);

    // Jump back to loop start (also the continue target)
    int offset = loop_start - current_pc() - 1;
    emit_sBx(Op::Jump, offset, s.loc.line);

    patch_jump(exit_jump);

    // Patch all breaks to here (after loop)
    for (auto& bp : cur_fn_->break_patches.back())
        patch_jump(bp.instr_idx);
    cur_fn_->break_patches.pop_back();
    cur_fn_->continue_patches.pop_back();
    cur_fn_->continue_targets.pop_back();
}

void Compiler::compile_for(const ForStmt& s) {
    // Compile iterable, then:
    // For now: only supports numeric range (BinaryExpr with .. or ..<)
    // Generic iterator protocol is Phase 3.
    //
    // Emit:
    //   R[iter] = start
    //   R[limit] = end
    //   R[inc]   = exclusive ? -1 : 0   (flag encoded as bool)
    //   loop:
    //     if iter >= limit: jump exit
    //     R[var] = iter
    //     body
    //     iter = iter + 1
    //     jump loop

    // Check if iterable is a range expression
    const BinaryExpr* range = dynamic_cast<const BinaryExpr*>(s.iterable.get());
    bool exclusive = range && (range->op == TokenKind::DotDotLt);
    bool is_range  = range && (range->op == TokenKind::DotDot || exclusive);

    if (is_range) {
        uint8_t iter_reg  = alloc_reg();
        uint8_t limit_reg = alloc_reg();
        uint8_t cmp_reg   = alloc_reg();

        compile_expr(*range->left,  iter_reg);
        compile_expr(*range->right, limit_reg);

        int loop_start = current_pc();

        // Push loop context — continue target is the increment step, which we'll
        // record after the body. For now push a sentinel (-1) and fix it up.
        cur_fn_->break_patches.push_back({});
        cur_fn_->continue_patches.push_back({});
        cur_fn_->continue_targets.push_back(-1); // filled in after body

        // cmp_reg = iter_reg < limit_reg  (for ..<) or <= (for ..)
        Op cmp_op = exclusive ? Op::Lt : Op::Le;
        emit_ABC(cmp_op, cmp_reg, iter_reg, limit_reg, s.loc.line);
        size_t exit_jump = emit_jump(Op::JumpFalse, cmp_reg, s.loc.line);

        // Expose loop variable
        push_scope();
        uint8_t var_reg = alloc_reg();
        emit_ABC(Op::Move, var_reg, iter_reg, 0, s.loc.line);
        define_local(s.var_name, var_reg, s.binding_is_let);

        // Body
        for (auto& stmt : s.body.stmts) compile_stmt(*stmt);

        pop_scope();

        // The increment step — this is the continue target
        int increment_pc = current_pc();
        cur_fn_->continue_targets.back() = increment_pc;
        // Patch all pending continue jumps to land here
        auto& code = cur_fn_->proto->code;
        for (auto& cp : cur_fn_->continue_patches.back()) {
            int off = increment_pc - (int)cp.instr_idx - 1;
            code[cp.instr_idx] = encode_AsBx(Op::Jump, 0, off);
        }

        // iter = iter + 1
        uint16_t k1 = add_constant(Value::from_int(1));
        uint8_t one_reg = alloc_reg();
        emit_ABx(Op::LoadK, one_reg, k1, s.loc.line);
        emit_ABC(Op::Add, iter_reg, iter_reg, one_reg, s.loc.line);
        free_reg(one_reg);

        // Jump back to condition check
        int offset = loop_start - current_pc() - 1;
        emit_sBx(Op::Jump, offset, s.loc.line);
        patch_jump(exit_jump);

        // Patch breaks to here
        for (auto& bp : cur_fn_->break_patches.back())
            patch_jump(bp.instr_idx);
        cur_fn_->break_patches.pop_back();
        cur_fn_->continue_patches.pop_back();
        cur_fn_->continue_targets.pop_back();

        free_reg(cmp_reg);
        free_reg(limit_reg);
        free_reg(iter_reg);
    } else if (!s.key_name.empty()) {
        // Key-value iteration: for k, v in table { }
        //   tbl  = iterable
        //   keys = TableKeys(tbl)   // array of hash keys
        //   idx  = 0
        //   len  = #keys
        //   loop:
        //     if idx >= len: exit
        //     k = keys[idx]
        //     v = tbl[k]
        //     body
        //   continue_target:
        //     idx = idx + 1
        //     jump loop
        uint8_t tbl_reg  = alloc_reg();
        uint8_t keys_reg = alloc_reg();
        uint8_t idx_reg  = alloc_reg();
        uint8_t len_reg  = alloc_reg();
        uint8_t cmp_reg  = alloc_reg();

        compile_expr(*s.iterable, tbl_reg);
        emit_ABC(Op::TableKeys, keys_reg, tbl_reg, 0, s.loc.line);
        emit_AsBx(Op::LoadInt, idx_reg, 0, s.loc.line);
        emit_ABC(Op::TLen, len_reg, keys_reg, 0, s.loc.line);

        int loop_start = current_pc();
        cur_fn_->break_patches.push_back({});
        cur_fn_->continue_patches.push_back({});
        cur_fn_->continue_targets.push_back(-1);

        emit_ABC(Op::Lt, cmp_reg, idx_reg, len_reg, s.loc.line);
        size_t exit_jump = emit_jump(Op::JumpFalse, cmp_reg, s.loc.line);

        push_scope();
        uint8_t key_reg = alloc_reg();
        uint8_t val_reg = alloc_reg();
        emit_ABC(Op::GetIndex, key_reg, keys_reg, idx_reg, s.loc.line);
        emit_ABC(Op::GetIndex, val_reg, tbl_reg,  key_reg, s.loc.line);
        define_local(s.var_name,  key_reg, s.binding_is_let);
        define_local(s.key_name,  val_reg, s.binding_is_let);
        for (auto& stmt : s.body.stmts) compile_stmt(*stmt);
        pop_scope();

        int increment_pc = current_pc();
        cur_fn_->continue_targets.back() = increment_pc;
        for (auto& cp : cur_fn_->continue_patches.back()) {
            auto& code = cur_fn_->proto->code;
            int off = increment_pc - (int)cp.instr_idx - 1;
            code[cp.instr_idx] = encode_AsBx(Op::Jump, 0, off);
        }
        uint16_t k1     = add_constant(Value::from_int(1));
        uint8_t  one_reg = alloc_reg();
        emit_ABx(Op::LoadK, one_reg, k1, s.loc.line);
        emit_ABC(Op::Add, idx_reg, idx_reg, one_reg, s.loc.line);
        free_reg(one_reg);
        int offset = loop_start - current_pc() - 1;
        emit_sBx(Op::Jump, offset, s.loc.line);
        patch_jump(exit_jump);

        for (auto& bp : cur_fn_->break_patches.back())
            patch_jump(bp.instr_idx);
        cur_fn_->break_patches.pop_back();
        cur_fn_->continue_patches.pop_back();
        cur_fn_->continue_targets.pop_back();

        free_reg(cmp_reg);
        free_reg(len_reg);
        free_reg(idx_reg);
        free_reg(keys_reg);
        free_reg(tbl_reg);
    } else {
        // General iterable: table/array — emit an index-based loop
        //   tbl  = iterable
        //   idx  = 0
        //   len  = #tbl
        //   loop:
        //     if idx >= len: exit
        //     var = tbl[idx]
        //     body
        //   continue_target:
        //     idx = idx + 1
        //     jump loop
        uint8_t tbl_reg  = alloc_reg();
        uint8_t idx_reg  = alloc_reg();
        uint8_t len_reg  = alloc_reg();
        uint8_t cmp_reg  = alloc_reg();

        compile_expr(*s.iterable, tbl_reg);
        emit_AsBx(Op::LoadInt, idx_reg, 0, s.loc.line);  // idx = 0
        emit_ABC(Op::TLen, len_reg, tbl_reg, 0, s.loc.line);

        int loop_start = current_pc();

        cur_fn_->break_patches.push_back({});
        cur_fn_->continue_patches.push_back({});
        cur_fn_->continue_targets.push_back(-1); // filled after body

        // if idx >= len: exit  (i.e. exit if NOT idx < len)
        emit_ABC(Op::Lt, cmp_reg, idx_reg, len_reg, s.loc.line);
        size_t exit_jump = emit_jump(Op::JumpFalse, cmp_reg, s.loc.line);

        push_scope();
        uint8_t var_reg = alloc_reg();
        emit_ABC(Op::GetIndex, var_reg, tbl_reg, idx_reg, s.loc.line);
        define_local(s.var_name, var_reg, s.binding_is_let);
        for (auto& stmt : s.body.stmts) compile_stmt(*stmt);
        pop_scope();

        // Increment step — continue target
        int increment_pc = current_pc();
        cur_fn_->continue_targets.back() = increment_pc;
        for (auto& cp : cur_fn_->continue_patches.back()) {
            auto& code = cur_fn_->proto->code;
            int off = increment_pc - (int)cp.instr_idx - 1;
            code[cp.instr_idx] = encode_AsBx(Op::Jump, 0, off);
        }

        uint16_t k1 = add_constant(Value::from_int(1));
        uint8_t  one_reg = alloc_reg();
        emit_ABx(Op::LoadK, one_reg, k1, s.loc.line);
        emit_ABC(Op::Add, idx_reg, idx_reg, one_reg, s.loc.line);
        free_reg(one_reg);

        // Jump back to condition
        int offset = loop_start - current_pc() - 1;
        emit_sBx(Op::Jump, offset, s.loc.line);
        patch_jump(exit_jump);

        for (auto& bp : cur_fn_->break_patches.back())
            patch_jump(bp.instr_idx);
        cur_fn_->break_patches.pop_back();
        cur_fn_->continue_patches.pop_back();
        cur_fn_->continue_targets.pop_back();

        free_reg(cmp_reg);
        free_reg(len_reg);
        free_reg(idx_reg);
        free_reg(tbl_reg);
    }
}

void Compiler::compile_match(const MatchStmt& s) {
    // Evaluate subject into a register
    uint8_t subj_reg = alloc_reg();
    compile_expr(*s.subject, subj_reg);

    std::vector<size_t> done_jumps; // jumps to after the whole match

    for (auto& arm : s.arms) {
        size_t skip_jump = SIZE_MAX;

        if (!arm.is_wild) {
            // cmp_reg = (subj == pattern)
            uint8_t pat_reg  = alloc_reg();
            uint8_t cmp_reg  = alloc_reg();
            compile_expr(*arm.pattern, pat_reg);
            emit_ABC(Op::Eq, cmp_reg, subj_reg, pat_reg, s.loc.line);
            skip_jump = emit_jump(Op::JumpFalse, cmp_reg, s.loc.line);
            free_reg(cmp_reg);
            free_reg(pat_reg);
        }

        // Body
        compile_stmt(*arm.body);

        // Jump past remaining arms (unless this is the last)
        if (&arm != &s.arms.back()) {
            done_jumps.push_back(emit_sBx(Op::Jump, 0, s.loc.line));
        }

        if (skip_jump != SIZE_MAX) {
            patch_jump(skip_jump);
        }
    }

    // Patch all done-jumps to here
    for (size_t idx : done_jumps) patch_jump(idx);

    free_reg(subj_reg);
}

void Compiler::compile_engine_block(const EngineBlock& s) {
    // Strip at compile time based on engine mode
    bool active = (s.engine == "unity"  && engine_ == EngineMode::Unity) ||
                  (s.engine == "unreal" && engine_ == EngineMode::Unreal) ||
                  engine_ == EngineMode::None; // emit always in None mode (generic)
    if (active) {
        compile_block(s.body);
    }
    // Otherwise: silently skip
}

void Compiler::compile_throw(const ThrowStmt& s) {
    uint8_t reg = alloc_reg();
    compile_expr(*s.value, reg);
    emit_ABC(Op::Throw, reg, 0, 0, s.loc.line);
    free_reg(reg);
}

void Compiler::compile_try_catch(const TryCatchStmt& s) {
    // Layout:
    //   PushTry  catch_reg, offset_to_catch_block
    //   <try_block>
    //   PopTry
    //   Jump  over_catch
    //   <catch_block with catch_var in catch_reg>
    //   <done>

    uint8_t catch_reg = cur_reg(); // catch var will land here

    // Emit PushTry — patch sBx offset after we know the catch block PC.
    size_t push_idx = emit_AsBx(Op::PushTry, catch_reg, 0, s.loc.line);

    // Try block
    compile_block(s.try_block);

    // PopTry — end of protected region
    emit_ABC(Op::PopTry, 0, 0, 0, s.loc.line);

    // Jump over catch block
    size_t jump_over = emit_sBx(Op::Jump, 0, s.loc.line);

    // Catch block starts here — patch PushTry's sBx to point here
    int catch_pc = current_pc();
    {
        auto& code = cur_fn_->proto->code;
        int off = catch_pc - (int)push_idx - 1;
        code[push_idx] = encode_AsBx(Op::PushTry, catch_reg, off);
    }

    // Reset next_reg to catch_reg so that alloc_reg() gives exactly catch_reg.
    // This is correct: try-block locals are out of scope after an exception.
    cur_fn_->next_reg = catch_reg;

    // catch_reg holds the thrown value; define it as a local.
    push_scope();
    uint8_t bound_reg = alloc_reg(); // will equal catch_reg
    define_local(s.catch_var, bound_reg, /*is_let=*/true);
    compile_block(s.catch_block);
    pop_scope();

    patch_jump(jump_over);
}

// ===========================================================================
// Expressions
// ===========================================================================
uint8_t Compiler::compile_expr(const Expr& expr, std::optional<uint8_t> dest) {
    if (auto* e = dynamic_cast<const LitExpr*>(&expr))
        return compile_lit(*e, dest);
    if (auto* e = dynamic_cast<const IdentExpr*>(&expr))
        return compile_ident(*e, dest);
    if (auto* e = dynamic_cast<const SelfExpr*>(&expr)) {
        int r = resolve_local("self");
        if (r < 0) { error(e->loc, "'self' not available in this context"); r = 0; }
        if (!dest) return (uint8_t)r;  // self is already in a register — no alloc needed
        if ((uint8_t)r != *dest)
            emit_ABC(Op::Move, *dest, (uint8_t)r, 0, e->loc.line);
        return *dest;
    }
    if (auto* e = dynamic_cast<const BinaryExpr*>(&expr))
        return compile_binary(*e, dest);
    if (auto* e = dynamic_cast<const UnaryExpr*>(&expr))
        return compile_unary(*e, dest);
    if (auto* e = dynamic_cast<const AssignExpr*>(&expr))
        return compile_assign(*e, dest);
    if (auto* e = dynamic_cast<const FieldExpr*>(&expr))
        return compile_field(*e, dest);
    if (auto* e = dynamic_cast<const CallExpr*>(&expr))
        return compile_call(*e, dest);
    if (auto* e = dynamic_cast<const StringInterpExpr*>(&expr))
        return compile_string_interp(*e, dest);
    if (auto* e = dynamic_cast<const LambdaExpr*>(&expr))
        return compile_lambda(*e, dest);
    if (auto* e = dynamic_cast<const GroupExpr*>(&expr))
        return compile_group(*e, dest);
    if (auto* e = dynamic_cast<const IfExpr*>(&expr))
        return compile_if_expr(*e, dest);
    if (auto* e = dynamic_cast<const ArrayExpr*>(&expr))
        return compile_array(*e, dest);
    if (auto* e = dynamic_cast<const TableExpr*>(&expr))
        return compile_table_expr(*e, dest);
    if (auto* e = dynamic_cast<const IndexExpr*>(&expr)) {
        uint8_t obj = compile_expr(*e->object);
        uint8_t idx = compile_expr(*e->index);
        uint8_t reg = dest ? *dest : alloc_reg();
        emit_ABC(Op::GetIndex, reg, obj, idx, e->loc.line);
        free_reg(idx);
        free_reg(obj);
        return reg;
    }
    // Fallback
    uint8_t reg = dest ? *dest : alloc_reg();
    emit_ABx(Op::LoadNil, reg, 0, expr.loc.line);
    return reg;
}

uint8_t Compiler::compile_lit(const LitExpr& e, std::optional<uint8_t> dest) {
    uint8_t reg = dest ? *dest : alloc_reg();
    switch (e.kind) {
        case LitExpr::Kind::Nil:
            if (e.value == "__table__")
                emit_ABC(Op::NewTable, reg, 0, 0, e.loc.line);
            else
                emit_ABx(Op::LoadNil, reg, 0, e.loc.line);
            break;
        case LitExpr::Kind::True:
            emit_ABC(Op::LoadBool, reg, 1, 0, e.loc.line);
            break;
        case LitExpr::Kind::False:
            emit_ABC(Op::LoadBool, reg, 0, 0, e.loc.line);
            break;
        case LitExpr::Kind::Int: {
            int64_t v = std::stoll(e.value);
            // Small int can use LoadInt directly
            if (v >= -32767 && v <= 32767) {
                emit_AsBx(Op::LoadInt, reg, (int32_t)v, e.loc.line);
            } else {
                uint16_t k = add_constant(Value::from_int(v));
                emit_ABx(Op::LoadK, reg, k, e.loc.line);
            }
            break;
        }
        case LitExpr::Kind::Float: {
            double v = std::stod(e.value);
            uint16_t k = add_constant(Value::from_float(v));
            emit_ABx(Op::LoadK, reg, k, e.loc.line);
            break;
        }
        case LitExpr::Kind::String: {
            uint16_t k = str_const(e.value);
            emit_ABx(Op::LoadK, reg, k, e.loc.line);
            break;
        }
    }
    return reg;
}

uint8_t Compiler::compile_ident(const IdentExpr& e, std::optional<uint8_t> dest) {
    // 'super' refers to the parent class prototype
    if (e.name == "super" && !current_base_class_.empty()) {
        uint8_t reg = dest ? *dest : alloc_reg();
        uint16_t k = str_const(current_base_class_);
        emit_ABx(Op::GetGlobal, reg, k, e.loc.line);
        return reg;
    }
    int local = resolve_local(e.name);
    if (local >= 0) {
        uint8_t src = (uint8_t)local;
        if (dest && *dest != src) {
            emit_ABC(Op::Move, *dest, src, 0, e.loc.line);
            return *dest;
        }
        return src;
    }
    // Upvalue?
    int upval = resolve_upvalue(cur_fn_, e.name);
    if (upval >= 0) {
        uint8_t reg = dest ? *dest : alloc_reg();
        emit_ABC(Op::GetUpval, reg, (uint8_t)upval, 0, e.loc.line);
        return reg;
    }
    // Global
    uint8_t reg = dest ? *dest : alloc_reg();
    uint16_t k = str_const(e.name);
    emit_ABx(Op::GetGlobal, reg, k, e.loc.line);
    return reg;
}

uint8_t Compiler::compile_binary(const BinaryExpr& e, std::optional<uint8_t> dest) {
    // Short-circuit logical operators
    if (e.op == TokenKind::And) {
        uint8_t reg = dest ? *dest : alloc_reg();
        compile_expr(*e.left, reg);
        size_t jmp = emit_jump(Op::JumpFalse, reg, e.loc.line);
        compile_expr(*e.right, reg);
        patch_jump(jmp);
        return reg;
    }
    if (e.op == TokenKind::Or) {
        uint8_t reg = dest ? *dest : alloc_reg();
        compile_expr(*e.left, reg);
        size_t jmp = emit_jump(Op::JumpTrue, reg, e.loc.line);
        compile_expr(*e.right, reg);
        patch_jump(jmp);
        return reg;
    }
    if (e.op == TokenKind::QuestionQuestion) {
        // a ?? b: if a is not nil, use a; otherwise use b
        uint8_t reg     = dest ? *dest : alloc_reg();
        compile_expr(*e.left, reg);
        // Check: reg == nil?
        uint8_t nil_reg = alloc_reg();
        uint8_t cmp_reg = alloc_reg();
        emit_ABx(Op::LoadNil, nil_reg, 0, e.loc.line);
        emit_ABC(Op::Eq, cmp_reg, reg, nil_reg, e.loc.line);
        free_reg(nil_reg);
        // JumpFalse: if cmp is false (left != nil) skip the right side
        size_t skip = emit_jump(Op::JumpFalse, cmp_reg, e.loc.line);
        free_reg(cmp_reg);
        compile_expr(*e.right, reg);
        patch_jump(skip);
        return reg;
    }

    // is-instance check: obj is ClassName
    if (e.op == TokenKind::KwIs) {
        uint8_t reg  = dest ? *dest : alloc_reg();
        uint8_t lreg = compile_expr(*e.left);
        // Right side must be an identifier (class name)
        std::string class_name;
        if (auto* id = dynamic_cast<const IdentExpr*>(e.right.get())) {
            class_name = id->name;
        } else {
            throw std::runtime_error("'is' operator requires a class name on the right");
        }
        uint16_t kidx = str_const(class_name);
        emit_ABC(Op::IsInstance, reg, lreg, (uint8_t)kidx, e.loc.line);
        free_reg(lreg);
        return reg;
    }

    // Range operators: treated as table construction in Phase 2 (VM just needs the pair)
    // For now emit the left operand — full iteration is handled in compile_for.
    if (e.op == TokenKind::DotDot || e.op == TokenKind::DotDotLt) {
        uint8_t reg = dest ? *dest : alloc_reg();
        compile_expr(*e.left, reg);
        return reg;
    }

    uint8_t lreg = compile_expr(*e.left);
    uint8_t rreg = compile_expr(*e.right);
    uint8_t reg  = dest ? *dest : alloc_reg();

    Op op;
    switch (e.op) {
        case TokenKind::Plus:     op = Op::Add; break;
        case TokenKind::Minus:    op = Op::Sub; break;
        case TokenKind::Star:     op = Op::Mul; break;
        case TokenKind::Slash:    op = Op::Div; break;
        case TokenKind::Percent:  op = Op::Mod; break;
        case TokenKind::StarStar: op = Op::Pow; break;
        case TokenKind::Eq:      op = Op::Eq;  break;
        case TokenKind::NotEq:   op = Op::Ne;  break;
        case TokenKind::Lt:      op = Op::Lt;  break;
        case TokenKind::LtEq:    op = Op::Le;  break;
        case TokenKind::Gt:      // a > b  →  b < a
            emit_ABC(Op::Lt, reg, rreg, lreg, e.loc.line);
            free_reg(rreg); free_reg(lreg);
            return reg;
        case TokenKind::GtEq:    // a >= b →  b <= a
            emit_ABC(Op::Le, reg, rreg, lreg, e.loc.line);
            free_reg(rreg); free_reg(lreg);
            return reg;
        default:
            op = Op::Nop;
    }

    emit_ABC(op, reg, lreg, rreg, e.loc.line);
    free_reg(rreg);
    free_reg(lreg);
    return reg;
}

uint8_t Compiler::compile_unary(const UnaryExpr& e, std::optional<uint8_t> dest) {
    uint8_t operand = compile_expr(*e.operand);
    uint8_t reg = dest ? *dest : alloc_reg();
    if (e.op == TokenKind::Hash) {
        emit_ABC(Op::TLen, reg, operand, 0, e.loc.line);
    } else {
        Op op = (e.op == TokenKind::Bang) ? Op::Not : Op::Neg;
        emit_ABC(op, reg, operand, 0, e.loc.line);
    }
    free_reg(operand);
    return reg;
}

// Map a compound assignment op to its arithmetic opcode.
static Op compound_op(TokenKind k) {
    switch (k) {
        case TokenKind::PlusAssign:    return Op::Add;
        case TokenKind::MinusAssign:   return Op::Sub;
        case TokenKind::StarAssign:    return Op::Mul;
        case TokenKind::SlashAssign:   return Op::Div;
        case TokenKind::PercentAssign: return Op::Mod;
        default:                       return Op::Nop;
    }
}

uint8_t Compiler::compile_assign(const AssignExpr& e, std::optional<uint8_t> dest) {
    bool is_compound = (e.op != TokenKind::Assign);

    // Simple identifier assignment
    if (auto* id = dynamic_cast<const IdentExpr*>(e.target.get())) {
        uint8_t val_reg = compile_expr(*e.value);

        int local = resolve_local(id->name);
        if (local >= 0) {
            if (local_is_let(id->name))
                error(id->loc, "cannot reassign immutable binding '" + id->name + "'");
            if (is_compound) {
                uint8_t tmp = alloc_reg();
                emit_ABC(compound_op(e.op), tmp, (uint8_t)local, val_reg, e.loc.line);
                emit_ABC(Op::Move, (uint8_t)local, tmp, 0, e.loc.line);
                free_reg(tmp);
            } else if ((uint8_t)local != val_reg) {
                emit_ABC(Op::Move, (uint8_t)local, val_reg, 0, e.loc.line);
            }
            return into(val_reg, dest);
        }
        int upval = resolve_upvalue(cur_fn_, id->name);
        if (upval >= 0) {
            if (is_compound) {
                uint8_t cur = alloc_reg();
                emit_ABC(Op::GetUpval, cur, (uint8_t)upval, 0, e.loc.line);
                uint8_t tmp = alloc_reg();
                emit_ABC(compound_op(e.op), tmp, cur, val_reg, e.loc.line);
                emit_ABC(Op::SetUpval, tmp, (uint8_t)upval, 0, e.loc.line);
                free_reg(tmp);
                free_reg(cur);
            } else {
                emit_ABC(Op::SetUpval, val_reg, (uint8_t)upval, 0, e.loc.line);
            }
            return into(val_reg, dest);
        }
        // Global
        uint16_t k = str_const(id->name);
        if (is_compound) {
            uint8_t cur = alloc_reg();
            emit_ABx(Op::GetGlobal, cur, k, e.loc.line);
            uint8_t tmp = alloc_reg();
            emit_ABC(compound_op(e.op), tmp, cur, val_reg, e.loc.line);
            emit_ABx(Op::SetGlobal, tmp, k, e.loc.line);
            free_reg(tmp);
            free_reg(cur);
        } else {
            emit_ABx(Op::SetGlobal, val_reg, k, e.loc.line);
        }
        return into(val_reg, dest);
    }

    // Field assignment:  obj.field [op]= val
    if (auto* field = dynamic_cast<const FieldExpr*>(e.target.get())) {
        uint8_t obj_reg = compile_expr(*field->object);
        uint16_t name_k = str_const(field->field);
        uint8_t val_reg = compile_expr(*e.value);
        if (is_compound) {
            // Load current: cur_reg = obj.field
            uint8_t cur_reg = alloc_reg();
            emit_ABx(Op::GetField, cur_reg,
                     (uint16_t)((uint16_t)obj_reg << 8 | (name_k & 0xFF)), e.loc.line);
            // Apply op: val_reg = cur_reg OP val_reg
            uint8_t res_reg = alloc_reg();
            emit_ABC(compound_op(e.op), res_reg, cur_reg, val_reg, e.loc.line);
            free_reg(cur_reg);
            // Store back
            emit_ABx(Op::SetField, obj_reg, (uint16_t)((name_k << 8) | res_reg), e.loc.line);
            free_reg(res_reg);
        } else {
            emit_ABx(Op::SetField, obj_reg, (uint16_t)((name_k << 8) | val_reg), e.loc.line);
        }
        free_reg(val_reg);
        free_reg(obj_reg);
        return into(val_reg, dest);
    }

    // Index assignment:  obj[idx] [op]= val
    if (auto* idx_expr = dynamic_cast<const IndexExpr*>(e.target.get())) {
        uint8_t obj_reg = compile_expr(*idx_expr->object);
        uint8_t idx_reg = compile_expr(*idx_expr->index);
        uint8_t val_reg = compile_expr(*e.value);
        if (is_compound) {
            uint8_t cur_reg = alloc_reg();
            emit_ABC(Op::GetIndex, cur_reg, obj_reg, idx_reg, e.loc.line);
            uint8_t res_reg = alloc_reg();
            emit_ABC(compound_op(e.op), res_reg, cur_reg, val_reg, e.loc.line);
            free_reg(cur_reg);
            emit_ABC(Op::SetIndex, obj_reg, idx_reg, res_reg, e.loc.line);
            free_reg(res_reg);
        } else {
            emit_ABC(Op::SetIndex, obj_reg, idx_reg, val_reg, e.loc.line);
        }
        free_reg(val_reg);
        free_reg(idx_reg);
        free_reg(obj_reg);
        return into(val_reg, dest);
    }

    uint8_t val_reg = compile_expr(*e.value);
    error(e.loc, "invalid assignment target");
    return into(val_reg, dest);
}

uint8_t Compiler::compile_field(const FieldExpr& e, std::optional<uint8_t> dest) {
    uint8_t obj_reg = compile_expr(*e.object);
    uint8_t reg     = dest ? *dest : alloc_reg();
    uint16_t name_k = str_const(e.field);

    if (e.access == FieldExpr::Access::Force) {
        // Emit CheckNil then GetField
        emit_ABx(Op::CheckNil, obj_reg, name_k, e.loc.line);
    }
    // Safe access (?.) — if obj is nil, result is nil; else fetch field
    if (e.access == FieldExpr::Access::Safe) {
        uint8_t cmp_reg = alloc_reg();
        emit_ABx(Op::LoadNil, cmp_reg, 0, e.loc.line);
        emit_ABC(Op::Eq, cmp_reg, obj_reg, cmp_reg, e.loc.line);  // cmp = (obj == nil)
        size_t jmp_nil = emit_jump(Op::JumpTrue, cmp_reg, e.loc.line); // jump if nil
        free_reg(cmp_reg);
        // Non-nil branch: fetch field
        emit_ABx(Op::GetField, reg, (uint16_t)((uint16_t)obj_reg << 8 | (name_k & 0xFF)), e.loc.line);
        size_t jmp_end = emit_sBx(Op::Jump, 0, e.loc.line);
        // Nil branch: result = nil
        patch_jump(jmp_nil);
        emit_ABx(Op::LoadNil, reg, 0, e.loc.line);
        patch_jump(jmp_end);
        free_reg(obj_reg);
        return reg;
    }

    // Normal dot access:
    // Encode: A=dest reg, upper 8 of Bx=obj_reg, lower 8=name_k (name_k < 256)
    emit_ABx(Op::GetField, reg, (uint16_t)((uint16_t)obj_reg << 8 | (name_k & 0xFF)), e.loc.line);
    free_reg(obj_reg);
    return reg;
}

uint8_t Compiler::compile_call(const CallExpr& e, std::optional<uint8_t> dest) {
    // ── Case 1: ClassName(args) → instantiation ──────────────────────────────
    // Detect bare identifier call where the name is a known class.
    if (auto* ident = dynamic_cast<const IdentExpr*>(e.callee.get())) {
        if (class_methods_.count(ident->name)) {
            // Emit a call to the class's __new__ helper (a native op sequence):
            //   inst = NewTable
            //   copy all methods from prototype onto inst
            //   set inst.__class__ = ClassName
            //   call inst.init(args...) if init exists
            //   return inst
            //
            // We do this inline at compile-time by emitting a synthetic
            // function stored under ClassName.__new__.
            // Simpler approach: emit a GetGlobal of the class table, then
            // emit an Op::NewInstance (which we handle in the VM).
            // For now, we reuse the existing table-copy approach via
            // a runtime helper: the class table IS the prototype; we build
            // an instance at runtime in the VM's Call handler when it sees
            // a table with __class__ as callee.  So just fall through to
            // the normal call path — the VM handles it.
        }
    }

    // ── Case 2: obj.method(args) → method call (self = obj) ──────────────
    // Layout: R[base]=method, R[base+1]=self(obj), R[base+2..]=user args
    // Uses Op::CallMethod so the VM knows to skip self for native callees.
    if (auto* field = dynamic_cast<const FieldExpr*>(e.callee.get())) {
        if (field->access == FieldExpr::Access::Dot ||
            field->access == FieldExpr::Access::Safe) {
            bool is_safe = (field->access == FieldExpr::Access::Safe);

            // Detect super.method(args) — fetch method from parent prototype but
            // use current self (reg 0) as the receiver, not the parent table.
            bool is_super = false;
            if (auto* id = dynamic_cast<const IdentExpr*>(field->object.get()))
                is_super = (id->name == "super" && !current_base_class_.empty());

            uint8_t base  = cur_reg();
            uint8_t m_reg = alloc_reg(); // base+0 = method
            uint8_t s_reg = alloc_reg(); // base+1 = self (obj)

            size_t jmp_nil = 0, jmp_end = 0;

            if (is_super) {
                // self = current frame's self (register 0)
                int self_local = resolve_local("self");
                emit_ABC(Op::Move, s_reg, (uint8_t)self_local, 0, field->loc.line);
                // fetch method from parent prototype
                uint8_t cls_reg = alloc_reg();
                emit_ABx(Op::GetGlobal, cls_reg,
                         str_const(current_base_class_), field->loc.line);
                uint16_t nk = str_const(field->field);
                emit_ABx(Op::GetField, m_reg,
                         (uint16_t)((cls_reg << 8) | nk), field->loc.line);
                free_reg(cls_reg);
            } else {
                compile_expr(*field->object, s_reg);
                if (is_safe) {
                    uint8_t cmp_reg = alloc_reg();
                    emit_ABx(Op::LoadNil, cmp_reg, 0, field->loc.line);
                    emit_ABC(Op::Eq, cmp_reg, s_reg, cmp_reg, field->loc.line);
                    jmp_nil = emit_jump(Op::JumpTrue, cmp_reg, field->loc.line);
                    free_reg(cmp_reg);
                }
                uint16_t nk = str_const(field->field);
                emit_ABx(Op::GetField, m_reg,
                         (uint16_t)((s_reg << 8) | nk), field->loc.line);
            }

            // Reset next_reg so user-args pack tightly after self slot.
            cur_fn_->next_reg = base + 2;
            for (auto& arg : e.args) {
                uint8_t ar = alloc_reg();
                compile_expr(*arg, ar);
            }
            uint8_t num_args = (uint8_t)e.args.size();
            emit_ABC(Op::CallMethod, base, num_args, 1, e.loc.line);
            cur_fn_->next_reg = base + 1;

            if (is_safe) {
                jmp_end = emit_sBx(Op::Jump, 0, e.loc.line);
                patch_jump(jmp_nil);
                emit_ABx(Op::LoadNil, m_reg, 0, e.loc.line); // result at base = nil
                patch_jump(jmp_end);
            }

            if (cur_fn_->max_reg < cur_fn_->next_reg)
                cur_fn_->max_reg = cur_fn_->next_reg;
            return into(base, dest);
        }
    }

    // ── Case 3: normal function call ─────────────────────────────────────────
    uint8_t base = cur_reg();
    uint8_t callee_reg = alloc_reg();
    compile_expr(*e.callee, callee_reg);
    // After compiling the callee (which may be a complex expression like a
    // chained call), next_reg may be above base+1. Reset it so that arg
    // registers start immediately after the callee slot.
    cur_fn_->next_reg = base + 1;
    for (auto& arg : e.args) {
        uint8_t arg_reg = alloc_reg();
        compile_expr(*arg, arg_reg);
    }
    uint8_t num_args = (uint8_t)e.args.size();
    emit_ABC(Op::Call, base, num_args, 1, e.loc.line);
    cur_fn_->next_reg = base + 1;
    if (cur_fn_->max_reg < cur_fn_->next_reg)
        cur_fn_->max_reg = cur_fn_->next_reg;
    return into(base, dest);
}

uint8_t Compiler::compile_string_interp(const StringInterpExpr& e, std::optional<uint8_t> dest) {
    if (e.parts.empty()) {
        uint8_t reg = dest ? *dest : alloc_reg();
        uint16_t k  = str_const("");
        emit_ABx(Op::LoadK, reg, k, e.loc.line);
        return reg;
    }

    // Compile each part, then concat left-to-right
    uint8_t acc = compile_expr(*e.parts[0]);
    for (size_t i = 1; i < e.parts.size(); ++i) {
        uint8_t part = compile_expr(*e.parts[i]);
        uint8_t tmp  = alloc_reg();
        emit_ABC(Op::Concat, tmp, acc, part, e.loc.line);
        free_reg(part);
        free_reg(acc);
        acc = tmp;
    }
    return into(acc, dest);
}

uint8_t Compiler::compile_lambda(const LambdaExpr& e, std::optional<uint8_t> dest) {
    uint8_t reg = dest ? *dest : alloc_reg();

    // Compile the lambda inline: create a new Proto directly
    Proto* fn_proto = chunk_->new_proto("<lambda>");
    fn_proto->num_params = (uint8_t)e.params.size();
    if (!e.params.empty() && e.params.back().is_vararg)
        fn_proto->is_vararg = true;

    FnState child_fs;
    child_fs.proto     = fn_proto;
    child_fs.enclosing = cur_fn_;
    FnState* prev = cur_fn_;
    cur_fn_ = &child_fs;

    push_scope();
    std::vector<uint8_t> param_regs;
    for (auto& param : e.params) {
        uint8_t r = alloc_reg();
        define_local(param.name, r, !param.is_mut);
        param_regs.push_back(r);
    }
    for (size_t i = 0; i < e.params.size(); ++i) {
        const auto& param = e.params[i];
        if (!param.default_val || param.is_vararg) continue;
        uint8_t r   = param_regs[i];
        uint8_t cmp = alloc_reg();
        emit_ABx(Op::LoadNil, cmp, 0, param.loc.line);
        emit_ABC(Op::Eq, cmp, r, cmp, param.loc.line);
        size_t jmp = emit_jump(Op::JumpFalse, cmp, param.loc.line);
        free_reg(cmp);
        compile_expr(*param.default_val, r);
        patch_jump(jmp);
    }

    compile_block(e.body);

    if (fn_proto->code.empty() ||
        instr_op(fn_proto->code.back()) != Op::Return) {
        emit_ABx(Op::Return, 0, 0);
    }

    pop_scope();
    fn_proto->max_regs = child_fs.max_reg;
    cur_fn_ = prev;

    cur_fn_->proto->protos.push_back(fn_proto);
    uint16_t nested_idx = (uint16_t)(cur_fn_->proto->protos.size() - 1);
    emit_ABx(Op::Closure, reg, nested_idx, e.loc.line);

    // Emit one pseudo-instruction per upvalue so the VM can populate the closure.
    // Op::Move  A=0 B=reg  → capture local `reg` from enclosing frame
    // Op::GetUpval A=0 B=upval_idx → inherit upvalue from enclosing closure
    for (auto& uv : fn_proto->upvalues) {
        if (uv.is_local)
            emit_ABC(Op::Move,     0, uv.idx, 0, e.loc.line);
        else
            emit_ABC(Op::GetUpval, 0, uv.idx, 0, e.loc.line);
    }

    return reg;
}

uint8_t Compiler::compile_group(const GroupExpr& e, std::optional<uint8_t> dest) {
    return compile_expr(*e.inner, dest);
}

uint8_t Compiler::compile_if_expr(const IfExpr& e, std::optional<uint8_t> dest) {
    uint8_t result = dest.value_or(alloc_reg());

    // Compile a block as a value: all but last stmt run normally;
    // last stmt is evaluated into `result`. Handles ExprStmt, IfStmt (recursive).
    std::function<void(const Block&)> compile_branch = [&](const Block& block) {
        if (block.stmts.empty()) {
            emit_ABx(Op::LoadNil, result, 0, e.loc.line);
            return;
        }
        for (size_t i = 0; i + 1 < block.stmts.size(); ++i)
            compile_stmt(*block.stmts[i]);
        auto* last = block.stmts.back().get();
        if (auto* es = dynamic_cast<const ExprStmt*>(last)) {
            compile_expr(*es->expr, result);
        } else if (auto* is = dynamic_cast<const IfStmt*>(last)) {
            // Nested if statement used as value — compile it as an if-expr.
            // Build a synthetic IfExpr and recurse.
            IfExpr nested;
            nested.loc        = is->loc;
            // We can't move from IfStmt (AST is const), so compile inline.
            uint8_t c = alloc_reg();
            compile_expr(*is->cond, c);
            size_t jf = emit_jump(Op::JumpFalse, c, is->loc.line);
            free_reg(c);
            push_scope();
            compile_branch(is->then_block);
            pop_scope();
            size_t je = emit_sBx(Op::Jump, 0, is->loc.line);
            patch_jump(jf);
            if (is->else_clause) {
                if (auto* bs = dynamic_cast<const BlockStmt*>(is->else_clause.get())) {
                    push_scope(); compile_branch(bs->block); pop_scope();
                } else {
                    // else if — compile the else clause as a stmt and load nil
                    compile_stmt(*is->else_clause);
                    emit_ABx(Op::LoadNil, result, 0, is->loc.line);
                }
            } else {
                emit_ABx(Op::LoadNil, result, 0, is->loc.line);
            }
            patch_jump(je);
        } else {
            compile_stmt(*last);
            emit_ABx(Op::LoadNil, result, 0, e.loc.line);
        }
    };

    // Condition
    uint8_t cond_reg = alloc_reg();
    compile_expr(*e.cond, cond_reg);
    size_t jump_false = emit_jump(Op::JumpFalse, cond_reg, e.loc.line);
    free_reg(cond_reg);

    push_scope(); compile_branch(e.then_block); pop_scope();

    size_t jump_end = emit_sBx(Op::Jump, 0, e.loc.line);
    patch_jump(jump_false);

    push_scope(); compile_branch(e.else_block); pop_scope();

    patch_jump(jump_end);
    return result;
}

uint8_t Compiler::compile_table_expr(const TableExpr& e, std::optional<uint8_t> dest) {
    uint8_t reg = dest.value_or(alloc_reg());
    emit_ABx(Op::NewTable, reg, 0, e.loc.line);
    for (auto& field : e.fields) {
        uint8_t val_reg = alloc_reg();
        compile_expr(*field.value, val_reg);
        uint16_t key_k = str_const(field.key);
        // SetField encoding: A=table_reg, Bx=(key_k<<8)|val_reg
        emit_ABx(Op::SetField, reg, (uint16_t)((key_k << 8) | val_reg), e.loc.line);
        free_reg(val_reg);
    }
    return reg;
}

uint8_t Compiler::compile_array(const ArrayExpr& e, std::optional<uint8_t> dest) {
    uint8_t reg = dest.value_or(alloc_reg());
    // R[reg] = {}
    emit_ABx(Op::NewTable, reg, 0, e.loc.line);

    // Emit each element as table.push equivalent:
    //   elem_reg = element value
    //   SetIndex R[reg][idx] = elem_reg   (using TLen to get current length)
    // Simpler: use a counter at compile time — index 0, 1, 2, ...
    for (size_t i = 0; i < e.elements.size(); ++i) {
        uint8_t elem_reg = alloc_reg();
        compile_expr(*e.elements[i], elem_reg);

        uint8_t idx_reg = alloc_reg();
        uint16_t k = add_constant(Value::from_int((int64_t)i));
        emit_ABx(Op::LoadK, idx_reg, k, e.loc.line);
        emit_ABC(Op::SetIndex, reg, idx_reg, elem_reg, e.loc.line);
        free_reg(idx_reg);
        free_reg(elem_reg);
    }

    return reg;
}

} // namespace zscript
