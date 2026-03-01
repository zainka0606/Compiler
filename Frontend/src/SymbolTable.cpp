#include "SymbolTable.h"

#include <utility>

namespace compiler::interpreter {

bool SymbolTable::DeclareFunction(std::string name) {
    return functions_.insert(std::move(name)).second;
}

bool SymbolTable::DeclareGlobal(std::string name) {
    return globals_.insert(std::move(name)).second;
}

bool SymbolTable::DeclareClass(std::string name,
                               std::vector<std::string> field_names,
                               std::vector<std::string> method_names) {
    const std::string key = name;
    return classes_
        .emplace(key,
                 ClassInfo{
                     .name = std::move(name),
                     .field_names = std::move(field_names),
                     .method_names = std::move(method_names),
                 })
        .second;
}

bool SymbolTable::IsFunction(std::string_view name) const {
    return functions_.find(std::string(name)) != functions_.end();
}

bool SymbolTable::IsGlobal(std::string_view name) const {
    return globals_.find(std::string(name)) != globals_.end();
}

const SymbolTable::ClassInfo *
SymbolTable::FindClass(std::string_view name) const {
    const auto it = classes_.find(std::string(name));
    if (it == classes_.end()) {
        return nullptr;
    }
    return &it->second;
}

const std::unordered_set<std::string> &SymbolTable::Functions() const {
    return functions_;
}

const std::unordered_set<std::string> &SymbolTable::Globals() const {
    return globals_;
}

const std::unordered_map<std::string, SymbolTable::ClassInfo> &
SymbolTable::Classes() const {
    return classes_;
}

} // namespace compiler::interpreter
