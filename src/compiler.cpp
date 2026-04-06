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
        } else if (dynamic_cast<const ImportDecl*>(decl.get())) {
            // Phase 3: module system
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

    // New FnState
    FnState child_fs;
    child_fs.proto     = fn_proto;
    child_fs.enclosing = cur_fn_;
    FnState* prev = cur_fn_;
    cur_fn_ = &child_fs;

    push_scope();
    // Parameters occupy the first registers
    for (auto& param : fn.params) {
        uint8_t r = alloc_reg();
        define_local(param.name, r, !param.is_mut);
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
    std::vector<std::string> method_names;
    for (auto& m : cls.members)
        if (auto* fn = dynamic_cast<const FnDecl*>(m.get()))
            method_names.push_back(fn->name);
    class_methods_[cls.name] = method_names;

    uint8_t tbl_reg = alloc_reg();
    emit_ABC(Op::NewTable, tbl_reg, 0, 0, line);

    // Compile each method with `self` injected as first parameter.
    for (auto& member : cls.members) {
        auto* fn = dynamic_cast<const FnDecl*>(member.get());
        if (!fn) continue;

        Proto* fn_proto = chunk_->new_proto(cls.name + "." + fn->name);
        fn_proto->num_params = (uint8_t)(fn->params.size() + 1); // +1 for self

        FnState child_fs;
        child_fs.proto     = fn_proto;
        child_fs.enclosing = cur_fn_;
        FnState* prev  = cur_fn_;
        cur_fn_        = &child_fs;
        current_class_ = cls.name;

        push_scope();
        // Register 0 = self
        uint8_t self_reg = alloc_reg();
        define_local("self", self_reg, /*is_let=*/false);
        // Remaining registers = declared params
        for (auto& param : fn->params) {
            uint8_t r = alloc_reg();
            define_local(param.name, r, !param.is_mut);
        }
        compile_block(fn->body);
        if (fn_proto->code.empty() ||
            instr_op(fn_proto->code.back()) != Op::Return)
            emit_ABx(Op::Return, 0, 0);
        pop_scope();

        fn_proto->max_regs = child_fs.max_reg;
        cur_fn_        = prev;
        current_class_ = "";

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
    } else if (auto* s = dynamic_cast<const BlockStmt*>(&stmt)) {
        compile_block(s->block);
    } else if (auto* s = dynamic_cast<const ExprStmt*>(&stmt)) {
        uint8_t r = alloc_reg();
        compile_expr(*s->expr, r);
        free_reg(r);
    }
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
    if (s.value) {
        uint8_t reg = alloc_reg();
        compile_expr(*s.value, reg);
        emit_ABx(Op::Return, reg, 1, s.loc.line);
        free_reg(reg);
    } else {
        emit_ABx(Op::Return, 0, 0, s.loc.line);
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

    uint8_t cond_reg = alloc_reg();
    compile_expr(*s.cond, cond_reg);
    size_t exit_jump = emit_jump(Op::JumpFalse, cond_reg, s.loc.line);
    free_reg(cond_reg);

    compile_block(s.body);

    // Jump back to loop start
    int offset = loop_start - current_pc() - 1;
    emit_sBx(Op::Jump, offset, s.loc.line);

    patch_jump(exit_jump);
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

        // iter = iter + 1
        uint16_t k1 = add_constant(Value::from_int(1));
        uint8_t one_reg = alloc_reg();
        emit_ABx(Op::LoadK, one_reg, k1, s.loc.line);
        emit_ABC(Op::Add, iter_reg, iter_reg, one_reg, s.loc.line);
        free_reg(one_reg);

        // Jump back
        int offset = loop_start - current_pc() - 1;
        emit_sBx(Op::Jump, offset, s.loc.line);
        patch_jump(exit_jump);

        free_reg(cmp_reg);
        free_reg(limit_reg);
        free_reg(iter_reg);
    } else {
        // General iterable: compile and iterate via table/array
        // For Phase 2, just compile the iterable and body without iteration logic
        uint8_t iter_reg = alloc_reg();
        compile_expr(*s.iterable, iter_reg);
        push_scope();
        uint8_t var_reg = alloc_reg();
        emit_ABx(Op::LoadNil, var_reg, 0, s.loc.line);
        define_local(s.var_name, var_reg, s.binding_is_let);
        for (auto& stmt : s.body.stmts) compile_stmt(*stmt);
        pop_scope();
        free_reg(iter_reg);
    }
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
    int local = resolve_local(e.name);
    if (local >= 0) {
        uint8_t src = (uint8_t)local;
        if (dest && *dest != src) {
            emit_ABC(Op::Move, *dest, src, 0, e.loc.line);
            return *dest;
        }
        return src;
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
        case TokenKind::Plus:    op = Op::Add; break;
        case TokenKind::Minus:   op = Op::Sub; break;
        case TokenKind::Star:    op = Op::Mul; break;
        case TokenKind::Slash:   op = Op::Div; break;
        case TokenKind::Percent: op = Op::Mod; break;
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
    Op op = (e.op == TokenKind::Bang) ? Op::Not : Op::Neg;
    emit_ABC(op, reg, operand, 0, e.loc.line);
    free_reg(operand);
    return reg;
}

uint8_t Compiler::compile_assign(const AssignExpr& e, std::optional<uint8_t> dest) {
    uint8_t val_reg = compile_expr(*e.value);

    // Simple identifier assignment
    if (auto* id = dynamic_cast<const IdentExpr*>(e.target.get())) {
        int local = resolve_local(id->name);
        if (local >= 0) {
            if (local_is_let(id->name)) {
                error(id->loc, "cannot reassign immutable binding '" + id->name + "'");
            }
            if ((uint8_t)local != val_reg)
                emit_ABC(Op::Move, (uint8_t)local, val_reg, 0, e.loc.line);
            return into(val_reg, dest);
        }
        // Global assignment
        uint16_t k = str_const(id->name);
        emit_ABx(Op::SetGlobal, val_reg, k, e.loc.line);
        return into(val_reg, dest);
    }

    // Field assignment:  obj.field = val
    if (auto* field = dynamic_cast<const FieldExpr*>(e.target.get())) {
        uint8_t obj_reg = compile_expr(*field->object);
        uint16_t name_k = str_const(field->field);
        // SetField: A=obj, B=val, Bx=name_k
        emit_ABx(Op::SetField, obj_reg, (uint16_t)((name_k << 8) | val_reg), e.loc.line);
        free_reg(obj_reg);
        return into(val_reg, dest);
    }

    // Index assignment:  obj[idx] = val
    if (auto* idx_expr = dynamic_cast<const IndexExpr*>(e.target.get())) {
        uint8_t obj_reg = compile_expr(*idx_expr->object);
        uint8_t idx_reg = compile_expr(*idx_expr->index);
        emit_ABC(Op::SetIndex, obj_reg, idx_reg, val_reg, e.loc.line);
        free_reg(idx_reg);
        free_reg(obj_reg);
        return into(val_reg, dest);
    }

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
    // Safe access: if nil, result is nil (emit conditional)
    if (e.access == FieldExpr::Access::Safe) {
        // if obj_reg == nil: reg = nil, else: reg = obj.field
        uint8_t cmp_reg = alloc_reg();
        emit_ABx(Op::LoadNil, cmp_reg, 0, e.loc.line);
        emit_ABC(Op::Ne, cmp_reg, obj_reg, cmp_reg, e.loc.line);
        size_t jmp = emit_jump(Op::JumpFalse, cmp_reg, e.loc.line);
        free_reg(cmp_reg);
        // Then branch: get field
        emit_ABx(Op::GetField, reg, name_k, e.loc.line);
        // Patch: skip over the nil-load if obj was not nil
        size_t jmp2 = emit_sBx(Op::Jump, 0, e.loc.line);
        patch_jump(jmp);
        // Else branch: reg = nil
        emit_ABx(Op::LoadNil, reg, 0, e.loc.line);
        patch_jump(jmp2);
        // GetField instruction: A=dest, B=obj, Bx=field_const
        // We need to fix the GetField above to include the obj register.
        // Patch: re-emit GetField with A=reg B=obj_reg Bx=name_k
        // This is a known limitation of single-pass emission; we'll fix the
        // GetField at the emitted position.
        size_t gf_idx = cur_fn_->proto->code.size() - 4;
        cur_fn_->proto->code[gf_idx] = encode_ABx(Op::GetField, reg, name_k);
        // Actually encode_ABx only takes A and Bx; we need to encode obj too.
        // Use our custom packing: encode A=reg, B=obj_reg via ABx where
        // upper 8 bits of Bx carry obj_reg and lower 8 carry name_k.
        // Simpler: use ABC(GetField, reg, obj_reg, name_k) — but name_k is 16-bit.
        // We accept this limitation: GetField uses A=dest, Bx=name_k, and the
        // VM must know that the object is in the register preceding dest, or
        // we redesign. For now we encode GetField as ABx where upper 8=obj, lower 8=name_k.
        // Repack:
        cur_fn_->proto->code[gf_idx] = encode_ABx(Op::GetField, reg, (uint16_t)((uint16_t)obj_reg << 8 | (name_k & 0xFF)));
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
        if (field->access == FieldExpr::Access::Dot) {
            uint8_t base  = cur_reg();
            uint8_t m_reg = alloc_reg(); // base+0 = method
            uint8_t s_reg = alloc_reg(); // base+1 = self (obj)
            compile_expr(*field->object, s_reg);
            uint16_t nk = str_const(field->field);
            emit_ABx(Op::GetField, m_reg,
                     (uint16_t)((s_reg << 8) | nk), field->loc.line);
            for (auto& arg : e.args) {
                uint8_t ar = alloc_reg();
                compile_expr(*arg, ar);
            }
            uint8_t num_args = (uint8_t)e.args.size(); // self is at base+1, not counted here
            emit_ABC(Op::CallMethod, base, num_args, 1, e.loc.line);
            cur_fn_->next_reg = base + 1;
            if (cur_fn_->max_reg < cur_fn_->next_reg)
                cur_fn_->max_reg = cur_fn_->next_reg;
            return into(base, dest);
        }
    }

    // ── Case 3: normal function call ─────────────────────────────────────────
    uint8_t base = cur_reg();
    uint8_t callee_reg = alloc_reg();
    compile_expr(*e.callee, callee_reg);
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

    FnState child_fs;
    child_fs.proto     = fn_proto;
    child_fs.enclosing = cur_fn_;
    FnState* prev = cur_fn_;
    cur_fn_ = &child_fs;

    push_scope();
    for (auto& param : e.params) {
        uint8_t r = alloc_reg();
        define_local(param.name, r, !param.is_mut);
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
    return reg;
}

uint8_t Compiler::compile_group(const GroupExpr& e, std::optional<uint8_t> dest) {
    return compile_expr(*e.inner, dest);
}

} // namespace zscript
