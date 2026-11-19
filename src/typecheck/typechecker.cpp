#include "c0/typecheck/typechecker.h"

#include "c0/ast.h"
#include "c0/driver.h"
#include "c0/memory/symbol_table.h"
#include "c0/typecheck/definite_init.h"
#include "c0/typecheck/type_context.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace c0 {

namespace {

template <typename T> void ensure_slot(std::vector<T> &vec, SymbolId id) {
  if (id != kInvalidSymbol && id >= vec.size()) {
    vec.resize(id + 1);
  }
}

void concretize_null(Exp *expr, const SemType *target) noexcept {
  if (!expr || !target || !target->is_pointer() || !expr->sem_type ||
      !expr->sem_type->is_null()) {
    return;
  }
  expr->sem_type = target;
  if (expr->ast_kind == ASTKind::TernaryExp) {
    auto *tern = static_cast<TernaryExp *>(expr);
    concretize_null(tern->true_branch, target);
    concretize_null(tern->false_branch, target);
  }
}

} // namespace

struct TypeChecker::Impl {
  struct FunctionInfo {
    const SemType *signature{nullptr};
    bool defined{false};
    const FnDef *def_node{nullptr};
    const FnDecl *decl_node{nullptr};
  };

  struct VarBinding {
    const SemType *type{nullptr};
    uint32_t slot_id{kInvalidSlot};
    SymbolKind kind{SymbolKind::LocalVar};
    uint32_t scope_depth{0};
  };

  struct ScopeTrailEntry {
    SymbolId sym{kInvalidSymbol};
    VarBinding prev_binding;
  };

  explicit Impl(Driver &d)
      : driver(d), symbols(d.symbols()), types(d.type_context()) {}

  Driver &driver;
  SymbolTable &symbols;
  TypeContext &types;

  // Dense SymbolId-indexed tables (zero string hashing/allocation overhead)
  std::vector<const TypeDef *> aliases;
  std::vector<const FnTypeDef *> function_aliases;
  std::vector<uint8_t> struct_declared;
  std::vector<uint8_t> resolving_aliases;
  std::vector<uint32_t> function_index; // SymbolId -> index+1 in functions
  std::vector<FunctionInfo> functions;

  // Flat O(1) scoped symbol table via undo trail
  std::vector<VarBinding> active_vars;
  std::vector<ScopeTrailEntry> scope_trail;
  std::vector<std::size_t> scope_marks;
  uint32_t current_scope_depth{0};
  uint32_t scope_floor_depth{0};

  uint32_t loop_depth{0};
  const SemType *current_return_type{nullptr};
  bool checking_function{false};
  uint32_t current_local_count{0};
  uint32_t next_lambda_id{0};
  Program *current_program{nullptr};

  void report(const ASTNode &node, const std::string &message) {
    driver.report_error(node.range.start, message);
  }

  template <typename Ptr>
  Ptr lookup_dense(const std::vector<Ptr> &vec, SymbolId id) const noexcept {
    return (id < vec.size()) ? vec[id] : nullptr;
  }

  bool is_struct_declared(SymbolId id) const noexcept {
    return id < struct_declared.size() && struct_declared[id] != 0;
  }

  FunctionInfo *find_function(SymbolId id) noexcept {
    if (id >= function_index.size() || function_index[id] == 0) {
      return nullptr;
    }
    return &functions[function_index[id] - 1];
  }

  void enter_scope() {
    scope_marks.push_back(scope_trail.size());
    ++current_scope_depth;
  }

  void exit_scope() {
    const std::size_t mark = scope_marks.back();
    scope_marks.pop_back();
    while (scope_trail.size() > mark) {
      const ScopeTrailEntry &entry = scope_trail.back();
      active_vars[entry.sym] = entry.prev_binding;
      scope_trail.pop_back();
    }
    --current_scope_depth;
  }

  const VarBinding *lookup_variable(SymbolId id) const noexcept {
    if (id >= active_vars.size()) {
      return nullptr;
    }
    const VarBinding &binding = active_vars[id];
    if (binding.type == nullptr || binding.scope_depth < scope_floor_depth) {
      return nullptr;
    }
    return &binding;
  }

  bool declare_variable(SymbolId id, const std::string &name,
                        const SemType *type, SymbolKind kind,
                        const ASTNode &node, uint32_t &out_slot) {
    out_slot = kInvalidSlot;
    ensure_slot(active_vars, id);

    const VarBinding &existing = active_vars[id];
    if (existing.type != nullptr && existing.scope_depth >= scope_floor_depth) {
      report(node, "variable '" + name +
                       "' is already declared in an enclosing or current scope");
      return false;
    }
    if (find_function(id) != nullptr) {
      report(node, "variable '" + name +
                       "' conflicts with a function declaration");
      return false;
    }
    if (type && type->is_void()) {
      report(node, "variable '" + name + "' cannot have type void");
      return false;
    }

    out_slot = current_local_count++;
    scope_trail.push_back({id, existing});
    active_vars[id] = VarBinding{type, out_slot, kind, current_scope_depth};
    return true;
  }

  const SemType *function_signature(Type *return_type,
                                    const std::vector<ParamDecl *> &params,
                                    SymbolId nominal_name = kInvalidSymbol) {
    const SemType *ret = resolve_type(return_type);
    if (params.size() <= 16) {
      const SemType *inline_params[16];
      for (std::size_t i = 0; i < params.size(); ++i) {
        inline_params[i] = resolve_type(params[i]->type);
      }
      return types.get_function(
          ret, std::span<const SemType *const>(inline_params, params.size()),
          nominal_name);
    }

    std::vector<const SemType *> resolved_params;
    resolved_params.reserve(params.size());
    for (const ParamDecl *param : params) {
      resolved_params.push_back(resolve_type(param->type));
    }
    return types.get_function(ret, resolved_params, nominal_name);
  }

  const SemType *resolve_type(const Type *type) {
    if (type == nullptr) {
      return types.invalid_type();
    }

    switch (type->kind) {
    case TypeKind::Int:
      return types.int_type();
    case TypeKind::Bool:
      return types.bool_type();
    case TypeKind::Char:
      return types.char_type();
    case TypeKind::String:
      return types.string_type();
    case TypeKind::Void:
      return types.void_type();
    case TypeKind::Pointer:
      return types.get_pointer(resolve_type(type->elem_type));
    case TypeKind::Array:
      return types.get_array(resolve_type(type->elem_type));
    case TypeKind::Struct: {
      const SymbolId id = symbols.intern(type->name);
      if (!is_struct_declared(id)) {
        report(*type, "unknown struct type '" + type->name + "'");
        return types.invalid_type();
      }
      return types.get_struct(id);
    }
    case TypeKind::Alias: {
      const SymbolId id = symbols.intern(type->name);
      if (const FnTypeDef *fn_alias = lookup_dense(function_aliases, id)) {
        return function_signature(fn_alias->return_type, fn_alias->params, id);
      }
      const TypeDef *alias = lookup_dense(aliases, id);
      if (alias == nullptr) {
        report(*type, "unknown type name '" + type->name + "'");
        return types.invalid_type();
      }
      ensure_slot(resolving_aliases, id);
      if (resolving_aliases[id] != 0) {
        report(*type, "cyclic type alias '" + type->name + "'");
        return types.invalid_type();
      }
      resolving_aliases[id] = 1;
      const SemType *resolved = resolve_type(alias->target_type);
      resolving_aliases[id] = 0;
      return resolved;
    }
    case TypeKind::FunctionPointer:
      return types.invalid_type();
    }
    return types.invalid_type();
  }

  void check_function_signature(Type *return_type,
                                const std::vector<ParamDecl *> &params,
                                const ASTNode &declaration) {
    const SemType *ret = resolve_type(return_type);
    if (ret->is_large()) {
      report(declaration, "functions cannot return a struct type");
    }
    for (const ParamDecl *param : params) {
      const SemType *param_type = resolve_type(param->type);
      if (param_type->is_large()) {
        report(*param, "function parameters cannot have a struct type");
      }
      if (param_type->is_valid() && param_type->is_function()) {
        report(*param,
               "function parameters cannot have a function type; use a "
               "pointer to a function type");
      }
    }
  }

  bool is_lvalue(const Exp *exp) const noexcept {
    if (exp == nullptr || !exp->is_lvalue()) {
      return false;
    }
    if (exp->ast_kind == ASTKind::VarExp) {
      const auto *var = static_cast<const VarExp *>(exp);
      return var->symbol_kind == SymbolKind::LocalVar ||
             var->symbol_kind == SymbolKind::Param;
    }
    return true;
  }

  void collect_types(Program &program) {
    std::vector<StructFieldDesc> field_descs;

    for (TopDecl *decl : program.decls) {
      if (!decl) {
        continue;
      }
      switch (decl->ast_kind) {
      case ASTKind::StructDecl: {
        auto *struct_decl = static_cast<StructDecl *>(decl);
        const SymbolId id = symbols.intern(struct_decl->name);
        types.get_or_create_struct_desc(id);
        ensure_slot(struct_declared, id);
        struct_declared[id] = 1;
        if (lookup_dense(aliases, id) != nullptr ||
            lookup_dense(function_aliases, id) != nullptr) {
          report(*struct_decl,
                 "duplicate struct declaration '" + struct_decl->name + "'");
        }
        break;
      }
      case ASTKind::StructDef: {
        auto *struct_def = static_cast<StructDef *>(decl);
        const SymbolId id = symbols.intern(struct_def->name);
        StructDesc *desc = types.get_or_create_struct_desc(id);
        struct_def->symbol_id = id;
        struct_def->struct_desc = desc;
        ensure_slot(struct_declared, id);
        struct_declared[id] = 1;

        if (lookup_dense(aliases, id) != nullptr ||
            lookup_dense(function_aliases, id) != nullptr) {
          report(*struct_def, "duplicate type name '" + struct_def->name + "'");
        }
        const bool already_defined = desc->defined;
        if (already_defined) {
          report(*struct_def,
                 "duplicate definition of struct '" + struct_def->name + "'");
        }

        field_descs.clear();
        uint32_t field_idx = 0;
        for (FieldDecl *field : struct_def->fields) {
          const SemType *field_type = resolve_type(field->type);
          if (field_type->is_large()) {
            report(*field, "struct fields cannot have a struct type");
          }
          if (field_type->is_valid() && field_type->is_function()) {
            report(*field,
                   "struct fields cannot have a function type; use a pointer "
                   "to a function type");
          }

          const SymbolId fid = symbols.intern(field->name);
          const bool dup = std::any_of(
              field_descs.begin(), field_descs.end(),
              [fid](const StructFieldDesc &d) { return d.name == fid; });
          if (dup) {
            report(*field, "duplicate field '" + field->name +
                               "' in struct '" + struct_def->name + "'");
          } else {
            field->symbol_id = fid;
            field->field_index = field_idx;
            field->sem_type = field_type;
            field_descs.push_back(StructFieldDesc{fid, field_type, field_idx});
            ++field_idx;
          }
        }
        if (!already_defined) {
          types.set_struct_fields(desc, field_descs);
        }
        break;
      }
      case ASTKind::TypeDef: {
        auto *alias = static_cast<TypeDef *>(decl);
        const SymbolId id = symbols.intern(alias->alias_name);
        if (lookup_dense(aliases, id) != nullptr ||
            lookup_dense(function_aliases, id) != nullptr ||
            is_struct_declared(id)) {
          report(*alias, "duplicate type name '" + alias->alias_name + "'");
        } else {
          ensure_slot(aliases, id);
          aliases[id] = alias;
        }
        break;
      }
      case ASTKind::FnTypeDef: {
        auto *fn_alias = static_cast<FnTypeDef *>(decl);
        const SymbolId id = symbols.intern(fn_alias->name);
        if (lookup_dense(aliases, id) != nullptr ||
            lookup_dense(function_aliases, id) != nullptr ||
            is_struct_declared(id)) {
          report(*fn_alias, "duplicate type name '" + fn_alias->name + "'");
        } else {
          ensure_slot(function_aliases, id);
          function_aliases[id] = fn_alias;
        }
        break;
      }
      default:
        break;
      }
    }

    for (const TypeDef *alias : aliases) {
      if (alias) {
        resolve_type(alias->target_type);
      }
    }
    for (std::size_t i = 0; i < function_aliases.size(); ++i) {
      const FnTypeDef *fn_alias = function_aliases[i];
      if (fn_alias) {
        function_signature(fn_alias->return_type, fn_alias->params,
                           static_cast<SymbolId>(i));
        check_function_signature(fn_alias->return_type, fn_alias->params,
                                 *fn_alias);
      }
    }
  }

  void collect_functions(Program &program) {
    for (TopDecl *decl : program.decls) {
      if (!decl) {
        continue;
      }
      FnDecl *function_decl = nullptr;
      FnDef *function_def = nullptr;
      if (decl->ast_kind == ASTKind::FnDecl) {
        function_decl = static_cast<FnDecl *>(decl);
      } else if (decl->ast_kind == ASTKind::FnDef) {
        function_def = static_cast<FnDef *>(decl);
      } else {
        continue;
      }

      const std::string &name =
          function_decl ? function_decl->name : function_def->name;
      Type *ret_type =
          function_decl ? function_decl->return_type : function_def->return_type;
      const auto &params =
          function_decl ? function_decl->params : function_def->params;

      const SymbolId id = symbols.intern(name);
      const SemType *signature = function_signature(ret_type, params);
      check_function_signature(ret_type, params, *decl);

      if (function_decl) {
        function_decl->symbol_id = id;
        function_decl->fn_sig = signature;
      } else {
        function_def->symbol_id = id;
        function_def->fn_sig = signature;
      }

      ensure_slot(function_index, id);
      if (function_index[id] == 0) {
        functions.push_back(FunctionInfo{signature, false, nullptr, function_decl});
        function_index[id] = static_cast<uint32_t>(functions.size());
      } else {
        FunctionInfo &existing = functions[function_index[id] - 1];
        if (!TypeContext::same_type(existing.signature, signature)) {
          report(*decl, "conflicting declaration of function '" + name + "'");
        }
        if (existing.decl_node == nullptr && function_decl != nullptr) {
          existing.decl_node = function_decl;
        }
      }

      if (function_def != nullptr) {
        FunctionInfo &info = functions[function_index[id] - 1];
        if (info.defined) {
          report(*function_def,
                 "duplicate definition of function '" + name + "'");
        } else {
          info.defined = true;
          info.signature = signature;
          info.def_node = function_def;
        }
      }
    }
  }

  bool check_main() {
    const SymbolId main_id = symbols.find("main");
    FunctionInfo *main_info =
        (main_id != kInvalidSymbol) ? find_function(main_id) : nullptr;
    if (main_info == nullptr || !main_info->defined) {
      driver.report_error(SourceLocation{1, 1},
                          "program must define int main() with no arguments");
      return false;
    }
    const SemType *sig = main_info->signature;
    if (!sig->is_function() || !sig->return_type->is_int() ||
        sig->param_count != 0) {
      if (main_info->def_node != nullptr) {
        report(*main_info->def_node, "main must have type int main()");
      }
      return false;
    }
    return true;
  }

  void reset_state(Program &program) {
    aliases.clear();
    function_aliases.clear();
    struct_declared.clear();
    resolving_aliases.clear();
    function_index.clear();
    functions.clear();
    active_vars.clear();
    scope_trail.clear();
    scope_marks.clear();
    current_scope_depth = 0;
    scope_floor_depth = 0;
    loop_depth = 0;
    current_return_type = types.invalid_type();
    checking_function = false;
    current_local_count = 0;
    next_lambda_id = 0;
    current_program = &program;
    program.hoisted_lambdas.clear();
  }

  bool check(Program &program) {
    const std::size_t initial_errors = driver.error_count();
    reset_state(program);

    bool saw_non_use_declaration = false;
    for (const TopDecl *decl : program.decls) {
      if (!decl) {
        continue;
      }
      if (decl->ast_kind == ASTKind::UseDirective) {
        if (saw_non_use_declaration) {
          report(*decl, "#use directives must precede all other declarations");
        }
      } else {
        saw_non_use_declaration = true;
      }
    }

    collect_types(program);
    collect_functions(program);
    check_main();

    for (TopDecl *decl : program.decls) {
      if (decl && decl->ast_kind == ASTKind::FnDef) {
        check_function(*static_cast<FnDef *>(decl));
      }
    }

    DefiniteInitChecker::check(program, driver);
    return driver.error_count() == initial_errors;
  }

  void bind_parameters(const std::vector<ParamDecl *> &params,
                       const SemType *sig) {
    for (std::size_t i = 0; i < params.size(); ++i) {
      ParamDecl *param = params[i];
      const SemType *param_ty =
          (sig && sig->is_function() && i < sig->param_count)
              ? sig->params[i]
              : resolve_type(param->type);
      const SymbolId pid = symbols.intern(param->name);
      uint32_t slot = kInvalidSlot;
      declare_variable(pid, param->name, param_ty, SymbolKind::Param, *param,
                       slot);
      param->symbol_id = pid;
      param->slot_id = slot;
      param->sem_type = param_ty;
    }
  }

  void check_function(FnDef &function) {
    enter_scope();
    const SemType *sig = function.fn_sig;
    current_return_type =
        (sig && sig->is_function()) ? sig->return_type
                                    : resolve_type(function.return_type);
    checking_function = true;
    loop_depth = 0;
    scope_floor_depth = current_scope_depth;
    current_local_count = 0;

    bind_parameters(function.params, sig);

    const bool must_return =
        function.body ? check_block(*function.body, false) : false;
    if (!current_return_type->is_void() && !must_return) {
      report(function, "not all control paths in non-void function '" +
                           function.name + "' return a value");
    }

    function.local_count = current_local_count;
    exit_scope();
    scope_floor_depth = 0;
    checking_function = false;
  }

  bool check_block(BlockStmt &block, bool create_scope = true) {
    if (create_scope) {
      enter_scope();
    }
    bool must_return = false;
    for (Stmt *statement : block.stmts) {
      if (statement == nullptr) {
        continue;
      }
      const bool statement_returns = check_statement(*statement);
      must_return = must_return || statement_returns;
    }
    if (create_scope) {
      exit_scope();
    }
    return must_return;
  }

  bool check_statement(Stmt &statement) {
    switch (statement.ast_kind) {
    case ASTKind::BlockStmt:
      return check_block(static_cast<BlockStmt &>(statement));

    case ASTKind::VarDeclStmt: {
      auto &decl = static_cast<VarDeclStmt &>(statement);
      const SemType *type = resolve_type(decl.type);
      if (type->is_large()) {
        report(decl, "variables cannot have a struct type");
      }
      if (type->is_valid() && type->is_function()) {
        report(decl, "variables cannot have a function type; use a pointer to a "
                     "function type");
      }
      if (decl.init != nullptr) {
        const SemType *value = check_expression(*decl.init);
        if (!TypeContext::compatible(type, value)) {
          report(decl, "initializer type does not match variable '" +
                           decl.name + "'");
        } else {
          concretize_null(decl.init, type);
        }
      }
      const SymbolId id = symbols.intern(decl.name);
      uint32_t slot = kInvalidSlot;
      declare_variable(id, decl.name, type, SymbolKind::LocalVar, decl, slot);
      decl.symbol_id = id;
      decl.slot_id = slot;
      decl.sem_type = type;
      return false;
    }

    case ASTKind::AssignStmt: {
      auto &assign = static_cast<AssignStmt &>(statement);
      const SemType *target = check_expression(*assign.lhs);
      const SemType *value = check_expression(*assign.rhs);
      if (!is_lvalue(assign.lhs)) {
        report(assign, "assignment target is not an lvalue");
      }
      if (assign.lhs->ast_kind == ASTKind::VarExp) {
        const auto *var = static_cast<const VarExp *>(assign.lhs);
        assign.lhs_slot_id = var->slot_id;
        assign.lhs_symbol_kind = var->symbol_kind;
      }
      if (assign.op == AssignOp::ASSIGN) {
        if (target->is_large() || value->is_large()) {
          report(assign, "struct values cannot be assigned");
        }
        if (!TypeContext::compatible(target, value)) {
          report(assign, "assignment types do not match");
        } else {
          concretize_null(assign.rhs, target);
        }
      } else if (!target->is_int() || !value->is_int()) {
        report(assign, "compound assignment requires int operands");
      }
      return false;
    }

    case ASTKind::IncDecStmt: {
      auto &inc_dec = static_cast<IncDecStmt &>(statement);
      const SemType *target = check_expression(*inc_dec.target);
      if (!is_lvalue(inc_dec.target) || !target->is_int()) {
        report(inc_dec, "increment and decrement require an int lvalue");
      }
      return false;
    }

    case ASTKind::ExprStmt: {
      auto &expr_stmt = static_cast<ExprStmt &>(statement);
      check_expression(*expr_stmt.expr);
      return false;
    }

    case ASTKind::IfStmt: {
      auto &cond_stmt = static_cast<IfStmt &>(statement);
      const SemType *condition = check_expression(*cond_stmt.cond);
      if (!condition->is_bool()) {
        report(*cond_stmt.cond, "if condition must have type bool");
      }
      const bool then_returns = check_statement(*cond_stmt.then_branch);
      const bool else_returns =
          cond_stmt.else_branch != nullptr &&
          check_statement(*cond_stmt.else_branch);
      return cond_stmt.else_branch != nullptr && then_returns && else_returns;
    }

    case ASTKind::WhileStmt: {
      auto &loop = static_cast<WhileStmt &>(statement);
      enter_scope();
      const SemType *condition = check_expression(*loop.cond);
      if (!condition->is_bool()) {
        report(*loop.cond, "while condition must have type bool");
      }
      ++loop_depth;
      check_statement(*loop.body);
      --loop_depth;
      exit_scope();
      return false;
    }

    case ASTKind::ForStmt: {
      auto &loop = static_cast<ForStmt &>(statement);
      enter_scope();
      if (loop.init != nullptr) {
        check_statement(*loop.init);
      }
      const SemType *condition = check_expression(*loop.cond);
      if (!condition->is_bool()) {
        report(*loop.cond, "for condition must have type bool");
      }
      if (loop.step != nullptr) {
        check_statement(*loop.step);
      }
      ++loop_depth;
      check_statement(*loop.body);
      --loop_depth;
      exit_scope();
      return false;
    }

    case ASTKind::ReturnStmt: {
      auto &ret = static_cast<ReturnStmt &>(statement);
      if (!checking_function) {
        report(ret, "return statement is not inside a function");
        return false;
      }
      if (ret.val == nullptr) {
        if (!current_return_type->is_void()) {
          report(ret, "non-void function must return a value");
          return false;
        }
      } else {
        const SemType *value = check_expression(*ret.val);
        if (current_return_type->is_void() ||
            !TypeContext::compatible(current_return_type, value)) {
          report(ret, "return value type does not match function return type");
          return false;
        }
        concretize_null(ret.val, current_return_type);
      }
      return true;
    }

    case ASTKind::AssertStmt: {
      auto &assertion = static_cast<AssertStmt &>(statement);
      const SemType *condition = check_expression(*assertion.cond);
      if (!condition->is_bool()) {
        report(*assertion.cond, "assert condition must have type bool");
      }
      return false;
    }

    case ASTKind::ErrorStmt: {
      auto &err = static_cast<ErrorStmt &>(statement);
      const SemType *message = check_expression(*err.msg);
      if (message->is_valid() && !message->is_string()) {
        report(*err.msg, "error message must have type string");
      }
      return true;
    }

    case ASTKind::BreakStmt:
      if (loop_depth == 0) {
        report(statement, "break statement must be inside a loop");
      }
      return false;

    case ASTKind::ContinueStmt:
      if (loop_depth == 0) {
        report(statement, "continue statement must be inside a loop");
      }
      return false;

    default:
      return false;
    }
  }

  const SemType *check_expression(Exp &expression) {
    switch (expression.ast_kind) {
    case ASTKind::IntLitExp:
      expression.sem_type = types.int_type();
      return expression.sem_type;

    case ASTKind::BoolLitExp:
      expression.sem_type = types.bool_type();
      return expression.sem_type;

    case ASTKind::CharLitExp:
      expression.sem_type = types.char_type();
      return expression.sem_type;

    case ASTKind::StringLitExp:
      expression.sem_type = types.string_type();
      return expression.sem_type;

    case ASTKind::NullLitExp:
      expression.sem_type = types.null_type();
      return expression.sem_type;

    case ASTKind::VarExp: {
      auto &var = static_cast<VarExp &>(expression);
      const SymbolId id = symbols.intern(var.name);
      var.symbol_id = id;
      if (const VarBinding *local = lookup_variable(id)) {
        var.slot_id = local->slot_id;
        var.symbol_kind = local->kind;
        var.sem_type = local->type;
        return var.sem_type;
      }
      if (FunctionInfo *fn = find_function(id)) {
        var.symbol_kind = SymbolKind::Function;
        var.resolved_fn_def = fn->def_node;
        var.resolved_fn_decl = fn->decl_node;
        var.sem_type = fn->signature;
        return var.sem_type;
      }
      report(var, "undeclared identifier '" + var.name + "'");
      var.sem_type = types.invalid_type();
      return var.sem_type;
    }

    case ASTKind::BinaryExp: {
      auto &binary = static_cast<BinaryExp &>(expression);
      const SemType *lhs = check_expression(*binary.lhs);
      const SemType *rhs = check_expression(*binary.rhs);
      switch (binary.op) {
      case BinaryOp::ADD:
      case BinaryOp::SUB:
      case BinaryOp::MUL:
      case BinaryOp::DIV:
      case BinaryOp::MOD:
      case BinaryOp::SHL:
      case BinaryOp::SHR:
      case BinaryOp::BIT_AND:
      case BinaryOp::BIT_XOR:
      case BinaryOp::BIT_OR:
        if (!lhs->is_int() || !rhs->is_int()) {
          report(binary, "arithmetic and bitwise operators require int operands");
          binary.sem_type = types.invalid_type();
          return binary.sem_type;
        }
        binary.sem_type = types.int_type();
        return binary.sem_type;

      case BinaryOp::LT:
      case BinaryOp::LE:
      case BinaryOp::GT:
      case BinaryOp::GE:
        if (!(lhs->is_int() && rhs->is_int()) &&
            !(lhs->is_char() && rhs->is_char())) {
          report(binary,
                 "relational operators require matching int or char operands");
        }
        binary.sem_type = types.bool_type();
        return binary.sem_type;

      case BinaryOp::EQ:
      case BinaryOp::NE:
        if (lhs->is_string() || rhs->is_string() || lhs->is_large() ||
            rhs->is_large()) {
          report(binary,
                 "equality operators do not support string or struct operands");
        } else if (!(lhs->is_null() && rhs->is_null()) &&
                   !TypeContext::compatible(lhs, rhs) &&
                   !TypeContext::compatible(rhs, lhs)) {
          report(binary, "equality operands have incompatible types");
        } else {
          if (lhs->is_null() && rhs->is_pointer()) {
            concretize_null(binary.lhs, rhs);
          } else if (rhs->is_null() && lhs->is_pointer()) {
            concretize_null(binary.rhs, lhs);
          }
        }
        binary.sem_type = types.bool_type();
        return binary.sem_type;

      case BinaryOp::LOG_AND:
      case BinaryOp::LOG_OR:
        if (!lhs->is_bool() || !rhs->is_bool()) {
          report(binary, "logical operators require bool operands");
        }
        binary.sem_type = types.bool_type();
        return binary.sem_type;
      }
      binary.sem_type = types.invalid_type();
      return binary.sem_type;
    }

    case ASTKind::UnaryExp: {
      auto &unary = static_cast<UnaryExp &>(expression);
      if (unary.op == UnaryOp::ADDR_OF) {
        if (unary.operand && unary.operand->ast_kind == ASTKind::VarExp) {
          auto *fn_var = static_cast<VarExp *>(unary.operand);
          const SymbolId id = symbols.intern(fn_var->name);
          if (lookup_variable(id) == nullptr) {
            if (FunctionInfo *found = find_function(id)) {
              if (!found->defined) {
                report(unary, "cannot take the address of undefined function '" +
                                  fn_var->name + "'");
                unary.sem_type = types.invalid_type();
                return unary.sem_type;
              }
              fn_var->symbol_id = id;
              fn_var->symbol_kind = SymbolKind::Function;
              fn_var->resolved_fn_def = found->def_node;
              fn_var->resolved_fn_decl = found->decl_node;
              fn_var->sem_type = found->signature;
              unary.sem_type = types.get_pointer(found->signature);
              return unary.sem_type;
            }
          }
          report(unary, "address-of operator requires a defined function identifier");
          unary.sem_type = types.invalid_type();
          return unary.sem_type;
        }
        if (unary.operand) {
          check_expression(*unary.operand);
        }
        report(unary, "address-of operator requires a defined function identifier");
        unary.sem_type = types.invalid_type();
        return unary.sem_type;
      }

      const SemType *operand = check_expression(*unary.operand);
      switch (unary.op) {
      case UnaryOp::LOG_NOT:
        if (!operand->is_bool()) {
          report(unary, "logical not requires a bool operand");
        }
        unary.sem_type = types.bool_type();
        return unary.sem_type;

      case UnaryOp::BIT_NOT:
      case UnaryOp::NEG:
        if (!operand->is_int()) {
          report(unary, "numeric unary operator requires an int operand");
        }
        unary.sem_type = types.int_type();
        return unary.sem_type;

      case UnaryOp::DEREF:
        if (operand->is_pointer()) {
          unary.sem_type = operand->elem;
          return unary.sem_type;
        }
        report(unary, "dereference requires a pointer operand");
        unary.sem_type = types.invalid_type();
        return unary.sem_type;

      case UnaryOp::ADDR_OF:
        break;
      }
      unary.sem_type = types.invalid_type();
      return unary.sem_type;
    }

    case ASTKind::TernaryExp: {
      auto &ternary = static_cast<TernaryExp &>(expression);
      const SemType *condition = check_expression(*ternary.cond);
      const SemType *true_type = check_expression(*ternary.true_branch);
      const SemType *false_type = check_expression(*ternary.false_branch);
      if (!condition->is_bool()) {
        report(*ternary.cond, "conditional expression condition must be bool");
      }
      if (true_type->is_null() && false_type->is_null()) {
        ternary.sem_type = types.null_type();
        return ternary.sem_type;
      }
      if (TypeContext::compatible(true_type, false_type)) {
        if (true_type->is_null()) {
          concretize_null(ternary.true_branch, false_type);
          ternary.sem_type = false_type;
        } else {
          concretize_null(ternary.false_branch, true_type);
          ternary.sem_type = true_type;
        }
        return ternary.sem_type;
      }
      if (TypeContext::compatible(false_type, true_type)) {
        if (false_type->is_null()) {
          concretize_null(ternary.false_branch, true_type);
          ternary.sem_type = true_type;
        } else {
          concretize_null(ternary.true_branch, false_type);
          ternary.sem_type = false_type;
        }
        return ternary.sem_type;
      }
      report(ternary, "conditional expression branches have incompatible types");
      ternary.sem_type = types.invalid_type();
      return ternary.sem_type;
    }

    case ASTKind::CallExp: {
      auto &call = static_cast<CallExp &>(expression);
      const SemType *callee = check_expression(*call.callee);
      const SemType *signature = nullptr;
      if (callee->is_function()) {
        signature = callee;
      } else if (callee->is_pointer() && callee->elem &&
                 callee->elem->is_function()) {
        signature = callee->elem;
      }

      if (call.callee->ast_kind == ASTKind::LambdaExp) {
        const auto *lam = static_cast<const LambdaExp *>(call.callee);
        call.call_kind = CallKind::InlineLambda;
        call.resolved_lambda = lam;
        call.callee_symbol = lam->synthesized_name;
      } else if (call.callee->ast_kind == ASTKind::VarExp) {
        const auto *var = static_cast<const VarExp *>(call.callee);
        if (var->symbol_kind == SymbolKind::Function) {
          call.call_kind = CallKind::DirectFn;
          call.callee_symbol = var->symbol_id;
          call.resolved_fn_def = var->resolved_fn_def;
          call.resolved_fn_decl = var->resolved_fn_decl;
        } else if (signature != nullptr) {
          call.call_kind = CallKind::IndirectFnPtr;
        }
      } else if (signature != nullptr) {
        call.call_kind = CallKind::IndirectFnPtr;
      }

      const SemType *inline_args[8];
      std::vector<const SemType *> heap_args;
      const SemType **arg_types = inline_args;
      if (call.args.size() > 8) {
        heap_args.resize(call.args.size());
        arg_types = heap_args.data();
      }
      for (std::size_t i = 0; i < call.args.size(); ++i) {
        arg_types[i] = check_expression(*call.args[i]);
      }

      if (signature == nullptr) {
        report(call, "called expression is not a function or function pointer");
        call.sem_type = types.invalid_type();
        return call.sem_type;
      }
      if (call.args.size() != signature->param_count) {
        report(call, "function called with the wrong number of arguments");
      }
      const std::size_t count =
          std::min<std::size_t>(call.args.size(), signature->param_count);
      for (std::size_t i = 0; i < count; ++i) {
        const SemType *param_ty = signature->params[i];
        if (!TypeContext::compatible(param_ty, arg_types[i])) {
          report(*call.args[i],
                 "argument type does not match function parameter");
        } else {
          concretize_null(call.args[i], param_ty);
        }
      }
      call.sem_type = signature->return_type;
      return call.sem_type;
    }

    case ASTKind::FieldExp: {
      auto &field = static_cast<FieldExp &>(expression);
      const SemType *base = check_expression(*field.base);
      if (field.is_arrow) {
        if (!base->is_pointer()) {
          report(field, "arrow operator requires a pointer to a struct");
          field.sem_type = types.invalid_type();
          return field.sem_type;
        }
        base = base->elem;
      }
      if (!base->is_struct()) {
        report(field, "field access requires a struct value");
        field.sem_type = types.invalid_type();
        return field.sem_type;
      }
      const StructDesc *desc = base->struct_desc;
      const std::string_view sname = symbols.resolve(desc->name);
      if (!desc->defined) {
        report(field, "field access on incomplete struct '" +
                          std::string(sname) + "'");
        field.sem_type = types.invalid_type();
        return field.sem_type;
      }
      const SymbolId fid = symbols.intern(field.field_name);
      const StructFieldDesc *member = desc->find_field(fid);
      if (member == nullptr) {
        report(field, "struct '" + std::string(sname) +
                          "' has no field named '" + field.field_name + "'");
        field.sem_type = types.invalid_type();
        return field.sem_type;
      }
      field.struct_desc = desc;
      field.field_index = member->index;
      field.sem_type = member->type;
      return field.sem_type;
    }

    case ASTKind::IndexExp: {
      auto &index = static_cast<IndexExp &>(expression);
      const SemType *base = check_expression(*index.base);
      const SemType *subscript = check_expression(*index.index);
      if (!subscript->is_int()) {
        report(*index.index, "array index must have type int");
      }
      if (base->is_array()) {
        if (base->elem->is_void()) {
          report(index, "cannot index a void array");
          index.sem_type = types.invalid_type();
          return index.sem_type;
        }
        index.sem_type = base->elem;
        return index.sem_type;
      }
      report(index, "indexing requires an array");
      index.sem_type = types.invalid_type();
      return index.sem_type;
    }

    case ASTKind::AllocExp: {
      auto &allocation = static_cast<AllocExp &>(expression);
      const SemType *allocated = resolve_type(allocation.alloc_type);
      if (allocated->is_void()) {
        report(allocation, "cannot allocate void");
        allocation.sem_type = types.invalid_type();
        return allocation.sem_type;
      }
      if (allocated->is_undefined_struct()) {
        report(allocation, "cannot allocate an undefined struct");
        allocation.sem_type = types.invalid_type();
        return allocation.sem_type;
      }
      allocation.sem_type = types.get_pointer(allocated);
      return allocation.sem_type;
    }

    case ASTKind::AllocArrayExp: {
      auto &allocation = static_cast<AllocArrayExp &>(expression);
      const SemType *count = check_expression(*allocation.count);
      if (!count->is_int()) {
        report(*allocation.count, "array allocation count must have type int");
      }
      const SemType *element = resolve_type(allocation.alloc_type);
      if (element->is_void()) {
        report(allocation, "cannot allocate an array of void");
        allocation.sem_type = types.invalid_type();
        return allocation.sem_type;
      }
      if (element->is_undefined_struct()) {
        report(allocation, "cannot allocate an undefined struct");
        allocation.sem_type = types.invalid_type();
        return allocation.sem_type;
      }
      allocation.sem_type = types.get_array(element);
      return allocation.sem_type;
    }

    case ASTKind::LambdaExp: {
      auto &lambda = static_cast<LambdaExp &>(expression);
      const SemType *signature =
          function_signature(lambda.return_type, lambda.params);
      check_function_signature(lambda.return_type, lambda.params, lambda);

      const SymbolId synth =
          symbols.intern("__c0_lambda_" + std::to_string(next_lambda_id++));
      lambda.synthesized_name = synth;
      lambda.fn_sig = signature;
      if (current_program != nullptr) {
        current_program->hoisted_lambdas.push_back(&lambda);
      }

      const uint32_t previous_floor = scope_floor_depth;
      const SemType *previous_return_type = current_return_type;
      const bool previous_checking_function = checking_function;
      const uint32_t previous_loop_depth = loop_depth;
      const uint32_t previous_local_count = current_local_count;

      enter_scope();
      scope_floor_depth = current_scope_depth;
      current_return_type =
          signature->is_function() ? signature->return_type
                                   : resolve_type(lambda.return_type);
      checking_function = true;
      loop_depth = 0;
      current_local_count = 0;

      bind_parameters(lambda.params, signature);

      const bool must_return =
          lambda.body ? check_block(*lambda.body, false) : false;
      if (!current_return_type->is_void() && !must_return) {
        report(lambda, "not all control paths in non-void lambda return a value");
      }

      lambda.local_count = current_local_count;
      exit_scope();

      scope_floor_depth = previous_floor;
      current_return_type = previous_return_type;
      checking_function = previous_checking_function;
      loop_depth = previous_loop_depth;
      current_local_count = previous_local_count;

      lambda.sem_type = types.get_pointer(signature);
      return lambda.sem_type;
    }

    default:
      report(expression, "unsupported expression");
      expression.sem_type = types.invalid_type();
      return expression.sem_type;
    }
  }
};

TypeChecker::TypeChecker(Driver &driver)
    : impl_(std::make_unique<Impl>(driver)) {}

TypeChecker::~TypeChecker() = default;

bool TypeChecker::check(Program &program) { return impl_->check(program); }

} // namespace c0
