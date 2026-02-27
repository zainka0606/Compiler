#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace compiler::interpreter {

class SymbolTable {
  public:
    struct ClassInfo {
        std::string name;
        std::vector<std::string> field_names;
        std::vector<std::string> method_names;
    };

    [[nodiscard]] bool DeclareFunction(std::string name);
    [[nodiscard]] bool DeclareGlobal(std::string name);
    [[nodiscard]] bool DeclareClass(std::string name, std::vector<std::string> field_names,
                                    std::vector<std::string> method_names);

    [[nodiscard]] bool IsFunction(std::string_view name) const;
    [[nodiscard]] bool IsGlobal(std::string_view name) const;
    [[nodiscard]] const ClassInfo* FindClass(std::string_view name) const;

    [[nodiscard]] const std::unordered_set<std::string>& Functions() const;
    [[nodiscard]] const std::unordered_set<std::string>& Globals() const;
    [[nodiscard]] const std::unordered_map<std::string, ClassInfo>& Classes() const;

  private:
    std::unordered_set<std::string> functions_;
    std::unordered_set<std::string> globals_;
    std::unordered_map<std::string, ClassInfo> classes_;
};

} // namespace compiler::interpreter
