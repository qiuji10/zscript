#pragma once
#include "chunk.h"
#include "value.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace zscript {

// ---------------------------------------------------------------------------
// Module — one compiled .zs file
//
// A module has:
//   - Its own global namespace (exported symbols)
//   - A compiled Chunk (bytecode)
//   - A lifecycle state
// ---------------------------------------------------------------------------

enum class ModuleState {
    Unloaded,
    Loading,   // currently executing (cycle detection)
    Loaded,
    Error,
};

struct Module {
    using State = ModuleState;

    std::string              name;       // logical name, e.g. "engine.core"
    std::string              filepath;   // resolved filesystem path
    ModuleState              state   = ModuleState::Unloaded;
    uint32_t                 version = 0; // incremented on every hotpatch reload
    std::unique_ptr<Chunk>   chunk;
    // Exported namespace: symbols defined at top level are placed here.
    std::unordered_map<std::string, Value> exports;

    // on_reload callback (set by script via on_reload(old) -> new)
    Value on_reload_fn;  // nil if not set
};

// ---------------------------------------------------------------------------
// ModuleLoader — resolves and compiles modules on demand.
//
// The VM owns one ModuleLoader.  When the script executes `import "path"`,
// the loader:
//  1. Resolves the name to a file path using search paths.
//  2. Compiles the file (using Lexer+Parser+Compiler).
//  3. Executes the module's top-level code in the module's own namespace.
//  4. Caches the module so subsequent imports are instant.
// ---------------------------------------------------------------------------

class VM;  // forward

class ModuleLoader {
public:
    // Add a directory to search for .zs modules.
    void add_search_path(const std::string& dir);

    // Resolve "engine.core" → "<search_dir>/engine/core.zs"
    // Returns empty string if not found.
    std::string resolve(const std::string& name) const;

    // Load (or return cached) a module.  Detects circular imports.
    // Returns nullptr on error (error_msg is set).
    Module* load(const std::string& name, VM& vm, std::string& error_msg);

    // Register a pre-built module (e.g. native module from C++ side).
    void register_native(const std::string& name,
                         std::unordered_map<std::string, Value> exports);

    const std::unordered_map<std::string, std::unique_ptr<Module>>& modules() const {
        return modules_;
    }

    // Find a cached module by name. Returns nullptr if not found.
    Module* find(const std::string& name) const;

    // Atomically replace a cached module (used by hotpatch).
    void replace(const std::string& name, std::shared_ptr<Module> new_mod);

    // Callback for loading a module source from a custom source
    // (useful for embedding without filesystem access).
    using SourceProvider = std::function<std::string(const std::string& path)>;
    void set_source_provider(SourceProvider fn) { source_provider_ = std::move(fn); }

private:
    std::vector<std::string>                               search_paths_;
    std::unordered_map<std::string, std::unique_ptr<Module>> modules_;
    std::unordered_set<std::string>                        loading_;  // cycle detection
    SourceProvider                                         source_provider_;
};

} // namespace zscript
