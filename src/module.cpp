#include "module.h"
#include "vm.h"
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include <fstream>
#include <sstream>

namespace zscript {

// ---------------------------------------------------------------------------
// Search paths
// ---------------------------------------------------------------------------
void ModuleLoader::add_search_path(const std::string& dir) {
    search_paths_.push_back(dir);
}

std::string ModuleLoader::resolve(const std::string& name) const {
    // Convert "engine.core" → "engine/core"
    std::string rel = name;
    for (char& c : rel) if (c == '.') c = '/';
    rel += ".zs";

    for (auto& dir : search_paths_) {
        std::string path = dir + "/" + rel;
        std::ifstream f(path);
        if (f.good()) return path;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Register a pre-built native module
// ---------------------------------------------------------------------------
void ModuleLoader::register_native(const std::string& name,
                                   std::unordered_map<std::string, Value> exports) {
    auto mod = std::make_unique<Module>();
    mod->name    = name;
    mod->state   = ModuleState::Loaded;
    mod->exports = std::move(exports);
    modules_[name] = std::move(mod);
}

// ---------------------------------------------------------------------------
// Load a module
// ---------------------------------------------------------------------------
Module* ModuleLoader::load(const std::string& name, VM& vm, std::string& error_msg) {
    // Return cached module
    auto it = modules_.find(name);
    if (it != modules_.end()) {
        auto* mod = it->second.get();
        if (mod->state == ModuleState::Loading) {
            error_msg = "circular import detected: '" + name + "'";
            return nullptr;
        }
        if (mod->state == ModuleState::Error) {
            error_msg = "module '" + name + "' previously failed to load";
            return nullptr;
        }
        return mod;
    }

    // Resolve path
    std::string source;
    std::string filepath;

    if (source_provider_) {
        source = source_provider_(name);
        filepath = name;
    } else {
        filepath = resolve(name);
        if (filepath.empty()) {
            error_msg = "module not found: '" + name + "'";
            return nullptr;
        }
        std::ifstream f(filepath);
        if (!f) {
            error_msg = "cannot open module file: '" + filepath + "'";
            return nullptr;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        source = ss.str();
    }

    // Create module entry
    auto mod_ptr = std::make_unique<Module>();
    mod_ptr->name     = name;
    mod_ptr->filepath = filepath;
    mod_ptr->state    = ModuleState::Loading;
    Module* mod = mod_ptr.get();
    modules_[name] = std::move(mod_ptr);

    // Lex + parse
    Lexer lexer(source, filepath);
    auto tokens = lexer.tokenize();
    if (lexer.has_errors()) {
        for (auto& e : lexer.errors())
            error_msg += filepath + ":" + std::to_string(e.loc.line) + ": " + e.message + "\n";
        mod->state = ModuleState::Error;
        return nullptr;
    }

    Parser parser(std::move(tokens), filepath);
    Program prog = parser.parse();
    if (parser.has_errors()) {
        for (auto& e : parser.errors())
            error_msg += filepath + ":" + std::to_string(e.loc.line) + ": " + e.message + "\n";
        mod->state = ModuleState::Error;
        return nullptr;
    }

    // Compile
    Compiler compiler(vm.active_tags());
    mod->chunk = compiler.compile(prog, filepath);
    if (compiler.has_errors()) {
        for (auto& e : compiler.errors())
            error_msg += filepath + ":" + std::to_string(e.loc.line) + ": " + e.message + "\n";
        mod->state = ModuleState::Error;
        return nullptr;
    }

    // Execute module top-level in a sandboxed namespace
    // The module's globals are merged into its exports after execution.
    if (!vm.execute_module(*mod, error_msg)) {
        mod->state = ModuleState::Error;
        return nullptr;
    }

    mod->state = ModuleState::Loaded;
    return mod;
}

// ---------------------------------------------------------------------------
// find / replace (used by HotpatchManager)
// ---------------------------------------------------------------------------
Module* ModuleLoader::find(const std::string& name) const {
    auto it = modules_.find(name);
    return (it != modules_.end()) ? it->second.get() : nullptr;
}

void ModuleLoader::replace(const std::string& name, std::shared_ptr<Module> new_mod) {
    // Move the Module data into a fresh unique_ptr owned by the registry.
    // Module is move-constructible (all fields are movable or trivial).
    modules_[name] = std::make_unique<Module>(std::move(*new_mod));
}

} // namespace zscript
