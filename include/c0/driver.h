#pragma once

#include "c0/ast.h"
#include <deque>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace c0 {

struct ErrorDiagnostic {
    std::string filename;
    SourceLocation location;
    std::string message;
};

class Driver {
public:
    Driver();
    ~Driver();

    bool parse_file(const std::string& filepath);
    bool parse_string(const std::string& code, const std::string& sourcename = "<input>");

    void report_error(const SourceLocation& loc, const std::string& msg);
    void report_error(int line, int col, const std::string& msg);

    bool has_errors() const { return !diagnostics_.empty(); }
    size_t error_count() const { return diagnostics_.size(); }
    const std::vector<ErrorDiagnostic>& diagnostics() const { return diagnostics_; }
    void print_diagnostics(std::ostream& os) const;

    void set_ast(Program* prog) { root_ = prog; }
    Program* ast() const { return root_; }

    // Factory method using default heap allocator
    template <typename T, typename... Args>
    T* make_node(Args&&... args) {
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw_ptr = node.get();
        allocated_nodes_.push_back(std::move(node));
        return raw_ptr;
    }

    const std::string& intern_string(std::string s) {
        strings_.push_back(std::move(s));
        return strings_.back();
    }

    template <typename T>
    std::vector<T>* make_vector() {
        auto vec = std::make_unique<std::vector<T>>();
        auto* raw = vec.get();
        allocated_vectors_.push_back(std::shared_ptr<void>(std::move(vec)));
        return raw;
    }

    // Type name tracking for typedef
    bool is_type_name(const std::string& name) const;
    void add_type_name(const std::string& name);

    // Lexer state
    int comment_depth{0};
    SourceLocation comment_start_loc;
    std::string current_file{"<input>"};
    int column{1};

private:
    Program* root_{nullptr};
    std::vector<std::unique_ptr<ASTNode>> allocated_nodes_;
    std::deque<std::string> strings_;
    std::vector<std::shared_ptr<void>> allocated_vectors_;
    std::vector<ErrorDiagnostic> diagnostics_;
    std::unordered_set<std::string> type_names_;
};

} // namespace c0
