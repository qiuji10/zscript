#pragma once
#include "ast.h"
#include "chunk.h"
#include "value.h"
#include <optional>
#include <string>
#include <vector>

namespace zscript {

struct CompileError {
    SourceLoc   loc;
    std::string message;
};

// ---------------------------------------------------------------------------
// Compiler — walks the AST and emits bytecode into a Chunk.
// ---------------------------------------------------------------------------
class Compiler {
public:
    explicit Compiler(EngineMode engine = EngineMode::None);

    // Compile a full program. Returns the filled Chunk (or empty on error).
    std::unique_ptr<Chunk> compile(const Program& prog, const std::string& filename);

    const std::vector<CompileError>& errors() const { return errors_; }
    bool has_errors() const { return !errors_.empty(); }

private:
    // =========================================================================
    // FnState — per-function compilation state
    // =========================================================================
    struct Local {
        std::string name;
        uint8_t     reg;   // register slot
        bool        is_let;
        int         scope_depth;
    };

    struct FnState {
        Proto*               proto    = nullptr;
        uint8_t              next_reg = 0;
        uint8_t              max_reg  = 0;
        int                  scope    = 0;   // current scope depth
        std::vector<Local>   locals;

        // Patch-lists for break/continue (Phase 3+): placeholder
        // Jump back-patch stacks
        struct PatchSlot { size_t instr_idx; };
        std::vector<std::vector<PatchSlot>> break_patches;

        FnState* enclosing = nullptr;   // for nested functions
    };

    FnState* cur_fn_ = nullptr;

    // =========================================================================
    // Register allocation
    // =========================================================================
    uint8_t alloc_reg();
    void    free_reg(uint8_t r);
    uint8_t cur_reg() const;     // peek at next free reg without allocating

    // =========================================================================
    // Scope management
    // =========================================================================
    void push_scope();
    void pop_scope();

    // =========================================================================
    // Local variable management
    // =========================================================================
    void    define_local(const std::string& name, uint8_t reg, bool is_let);
    // Returns register of a local, or -1 if not found
    int     resolve_local(const std::string& name) const;
    bool    local_is_let(const std::string& name) const;

    // =========================================================================
    // Constant pool
    // =========================================================================
    uint16_t add_constant(Value v);
    uint16_t str_const(const std::string& s);

    // =========================================================================
    // Instruction emission
    // =========================================================================
    size_t emit(uint32_t instr, uint32_t line = 0);
    size_t emit_ABC(Op op, uint8_t A, uint8_t B, uint8_t C, uint32_t line = 0);
    size_t emit_ABx(Op op, uint8_t A, uint16_t Bx, uint32_t line = 0);
    size_t emit_AsBx(Op op, uint8_t A, int32_t sBx, uint32_t line = 0);
    size_t emit_sBx(Op op, int32_t sBx, uint32_t line = 0);

    // Emit a placeholder jump; returns the instruction index for patching.
    size_t emit_jump(Op op, uint8_t cond_reg, uint32_t line = 0);
    // Patch the jump at idx to jump to the current end of code.
    void   patch_jump(size_t idx);
    // Current code position (for back-edges in loops).
    int    current_pc() const;

    // =========================================================================
    // Compilation — declarations
    // =========================================================================
    void compile_fn_decl(const FnDecl& fn, uint8_t dest_reg, uint32_t line);
    void compile_class_decl(const ClassDecl& cls, uint32_t line);
    void compile_top_level(const Program& prog);

    // =========================================================================
    // Compilation — statements
    // =========================================================================
    void compile_stmt(const Stmt& stmt);
    void compile_block(const Block& block);
    void compile_var_decl(const VarDeclStmt& s);
    void compile_return(const ReturnStmt& s);
    void compile_if(const IfStmt& s);
    void compile_while(const WhileStmt& s);
    void compile_for(const ForStmt& s);
    void compile_engine_block(const EngineBlock& s);

    // =========================================================================
    // Compilation — expressions
    // Returns the register holding the result.
    // =========================================================================
    uint8_t compile_expr(const Expr& expr, std::optional<uint8_t> dest = std::nullopt);
    uint8_t compile_lit(const LitExpr& e, std::optional<uint8_t> dest);
    uint8_t compile_ident(const IdentExpr& e, std::optional<uint8_t> dest);
    uint8_t compile_binary(const BinaryExpr& e, std::optional<uint8_t> dest);
    uint8_t compile_unary(const UnaryExpr& e, std::optional<uint8_t> dest);
    uint8_t compile_assign(const AssignExpr& e, std::optional<uint8_t> dest);
    uint8_t compile_field(const FieldExpr& e, std::optional<uint8_t> dest);
    uint8_t compile_call(const CallExpr& e, std::optional<uint8_t> dest);
    uint8_t compile_string_interp(const StringInterpExpr& e, std::optional<uint8_t> dest);
    uint8_t compile_lambda(const LambdaExpr& e, std::optional<uint8_t> dest);
    uint8_t compile_group(const GroupExpr& e, std::optional<uint8_t> dest);

    // Emit result into dest_reg (move if necessary); returns dest_reg.
    uint8_t into(uint8_t result_reg, std::optional<uint8_t> dest);

    // =========================================================================
    // Error handling
    // =========================================================================
    void error(const SourceLoc& loc, const std::string& msg);

    // =========================================================================
    // State
    // =========================================================================
    EngineMode              engine_    = EngineMode::None;
    std::unique_ptr<Chunk>  chunk_;
    std::vector<CompileError> errors_;
};

} // namespace zscript
