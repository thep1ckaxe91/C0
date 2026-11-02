#include "c0/typecheck/typechecker.h"

#include "c0/ast.h"
#include "c0/driver.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace c0 {

struct TypeChecker::Impl {
  struct TypeInfo {
    TypeKind kind{TypeKind::Void};
    std::shared_ptr<TypeInfo> element;
    std::string name;
    std::string nominal_name;
    std::vector<TypeInfo> parameters;
    std::shared_ptr<TypeInfo> result;
    bool valid{true};
    bool is_null{false};
  };

  struct FunctionInfo {
    TypeInfo signature;
    bool defined{false};
  };

  struct StructInfo {
    bool defined{false};
    std::unordered_map<std::string, TypeInfo> fields;
  };

  explicit Impl(Driver &driver) : driver(driver) {}

  Driver &driver;
  std::unordered_map<std::string, const TypeDef *> aliases;
  std::unordered_map<std::string, const FnTypeDef *> function_aliases;
  std::unordered_map<std::string, StructInfo> structs;
  std::unordered_map<std::string, FunctionInfo> functions;
  std::vector<std::unordered_map<std::string, TypeInfo>> scopes;
  size_t scope_floor{0};
  size_t loop_depth{0};
  TypeInfo current_return_type;
  bool checking_function{false};
  std::unordered_set<std::string> resolving_aliases;

  static TypeInfo invalid_type() {
    TypeInfo type;
    type.valid = false;
    return type;
  }

  static TypeInfo null_type() {
    TypeInfo type;
    type.is_null = true;
    return type;
  }

  static TypeInfo primitive(TypeKind kind) {
    TypeInfo type;
    type.kind = kind;
    return type;
  }

  static TypeInfo pointer_to(TypeInfo element) {
    TypeInfo type;
    type.kind = TypeKind::Pointer;
    type.valid = element.valid;
    type.element = std::make_shared<TypeInfo>(std::move(element));
    return type;
  }

  void report(const ASTNode &node, const std::string &message) {
    driver.report_error(node.range.start, message);
  }

  bool same_type(const TypeInfo &lhs, const TypeInfo &rhs) const {
    if (!lhs.valid || !rhs.valid || lhs.is_null || rhs.is_null ||
        lhs.kind != rhs.kind) {
      return false;
    }

    switch (lhs.kind) {
    case TypeKind::Pointer:
    case TypeKind::Array:
      return lhs.element && rhs.element && same_type(*lhs.element, *rhs.element);
    case TypeKind::Struct:
      return lhs.name == rhs.name;
    case TypeKind::FunctionPointer:
      if ((!lhs.nominal_name.empty() && !rhs.nominal_name.empty() &&
           lhs.nominal_name != rhs.nominal_name) ||
          !lhs.result || !rhs.result ||
          !same_type(*lhs.result, *rhs.result) ||
          lhs.parameters.size() != rhs.parameters.size()) {
        return false;
      }
      for (size_t i = 0; i < lhs.parameters.size(); ++i) {
        if (!same_type(lhs.parameters[i], rhs.parameters[i])) {
          return false;
        }
      }
      return true;
    default:
      return true;
    }
  }

  bool compatible(const TypeInfo &target, const TypeInfo &value) const {
    if (!target.valid || !value.valid) {
      return true;
    }
    if (value.is_null) {
      return target.kind == TypeKind::Pointer;
    }
    return same_type(target, value);
  }

  TypeInfo function_signature(Type *return_type,
                              const std::vector<ParamDecl *> &params) {
    TypeInfo signature;
    signature.kind = TypeKind::FunctionPointer;
    signature.result =
        std::make_shared<TypeInfo>(resolve_type(return_type));
    for (const ParamDecl *param : params) {
      signature.parameters.push_back(resolve_type(param->type));
    }
    if (!signature.result->valid ||
        std::any_of(signature.parameters.begin(), signature.parameters.end(),
                    [](const TypeInfo &param) { return !param.valid; })) {
      signature.valid = false;
    }
    return signature;
  }

  TypeInfo resolve_type(const Type *type) {
    if (type == nullptr) {
      return invalid_type();
    }

    switch (type->kind) {
    case TypeKind::Pointer:
    case TypeKind::Array: {
      TypeInfo resolved;
      resolved.kind = type->kind;
      resolved.element =
          std::make_shared<TypeInfo>(resolve_type(type->elem_type));
      resolved.valid = resolved.element->valid;
      return resolved;
    }
    case TypeKind::Alias: {
      const auto function_alias = function_aliases.find(type->name);
      if (function_alias != function_aliases.end()) {
        TypeInfo signature =
            function_signature(function_alias->second->return_type,
                               function_alias->second->params);
        signature.nominal_name = type->name;
        return signature;
      }
      const auto alias = aliases.find(type->name);
      if (alias == aliases.end()) {
        report(*type, "unknown type name '" + type->name + "'");
        return invalid_type();
      }
      if (!resolving_aliases.insert(type->name).second) {
        report(*type, "cyclic type alias '" + type->name + "'");
        return invalid_type();
      }
      TypeInfo resolved = resolve_type(alias->second->target_type);
      resolving_aliases.erase(type->name);
      return resolved;
    }
    case TypeKind::Struct:
      if (!structs.contains(type->name)) {
        report(*type, "unknown struct type '" + type->name + "'");
        return invalid_type();
      }
      {
        TypeInfo resolved;
        resolved.kind = TypeKind::Struct;
        resolved.name = type->name;
        return resolved;
      }
    default:
      return primitive(type->kind);
    }
  }

  bool is_void(const TypeInfo &type) const {
    return type.valid && !type.is_null && type.kind == TypeKind::Void;
  }

  bool is_large_type(const TypeInfo &type) const {
    return type.valid && type.kind == TypeKind::Struct;
  }

  bool is_undefined_struct(const TypeInfo &type) const {
    if (!is_large_type(type)) {
      return false;
    }
    const auto found = structs.find(type.name);
    return found == structs.end() || !found->second.defined;
  }

  void check_function_signature(Type *return_type,
                                const std::vector<ParamDecl *> &params,
                                const ASTNode &declaration) {
    if (is_large_type(resolve_type(return_type))) {
      report(declaration, "functions cannot return a struct type");
    }
    for (const ParamDecl *param : params) {
      const TypeInfo param_type = resolve_type(param->type);
      if (is_large_type(param_type)) {
        report(*param, "function parameters cannot have a struct type");
      }
      if (param_type.valid && param_type.kind == TypeKind::FunctionPointer) {
        report(*param,
               "function parameters cannot have a function type; use a "
               "pointer to a function type");
      }
    }
  }

  bool is_int(const TypeInfo &type) const {
    return type.valid && type.kind == TypeKind::Int;
  }

  bool is_char(const TypeInfo &type) const {
    return type.valid && type.kind == TypeKind::Char;
  }

  bool is_bool(const TypeInfo &type) const {
    return type.valid && type.kind == TypeKind::Bool;
  }

  bool is_lvalue(const Exp *exp) const {
    if (exp == nullptr || !exp->is_lvalue()) {
      return false;
    }
    if (const auto *variable = dynamic_cast<const VarExp *>(exp)) {
      for (size_t index = scopes.size(); index > scope_floor; --index) {
        if (scopes[index - 1].contains(variable->name)) {
          return true;
        }
      }
      return false;
    }
    return true;
  }

  std::optional<TypeInfo> lookup_variable(const std::string &name) const {
    for (size_t index = scopes.size(); index > scope_floor; --index) {
      const auto &scope = scopes[index - 1];
      const auto found = scope.find(name);
      if (found != scope.end()) {
        return found->second;
      }
    }
    return std::nullopt;
  }

  bool declare_variable(const std::string &name, const TypeInfo &type,
                        const ASTNode &node) {
    if (scopes.empty()) {
      scopes.emplace_back();
    }
    for (size_t index = scope_floor; index < scopes.size(); ++index) {
      if (scopes[index].contains(name)) {
        report(node, "variable '" + name +
                         "' is already declared in an enclosing or current scope");
        return false;
      }
    }
    if (functions.contains(name)) {
      report(node, "variable '" + name +
                       "' conflicts with a function declaration");
      return false;
    }
    if (is_void(type)) {
      report(node, "variable '" + name + "' cannot have type void");
      return false;
    }
    scopes.back().emplace(name, type);
    return true;
  }

  void collect_types(const Program &program) {
    for (const TopDecl *decl : program.decls) {
      if (const auto *struct_decl = dynamic_cast<const StructDecl *>(decl)) {
        structs.try_emplace(struct_decl->name);
        if (aliases.contains(struct_decl->name) ||
            function_aliases.contains(struct_decl->name)) {
          report(*struct_decl, "duplicate struct declaration '" +
                                   struct_decl->name + "'");
        }
      } else if (const auto *struct_def =
                     dynamic_cast<const StructDef *>(decl)) {
        auto &info = structs[struct_def->name];
        if (aliases.contains(struct_def->name) ||
            function_aliases.contains(struct_def->name)) {
          report(*struct_def, "duplicate type name '" + struct_def->name + "'");
        }
        if (info.defined) {
          report(*struct_def,
                 "duplicate definition of struct '" + struct_def->name + "'");
        } else {
          info.defined = true;
        }
        for (const FieldDecl *field : struct_def->fields) {
          const TypeInfo field_type = resolve_type(field->type);
          if (is_large_type(field_type)) {
            report(*field, "struct fields cannot have a struct type");
          }
          if (field_type.valid &&
              field_type.kind == TypeKind::FunctionPointer) {
            report(*field,
                   "struct fields cannot have a function type; use a pointer "
                   "to a function type");
          }
          if (info.fields.contains(field->name)) {
            report(*field, "duplicate field '" + field->name +
                               "' in struct '" + struct_def->name + "'");
          } else {
            info.fields.emplace(field->name, field_type);
          }
        }
      } else if (const auto *alias = dynamic_cast<const TypeDef *>(decl)) {
        if (aliases.contains(alias->alias_name) ||
            function_aliases.contains(alias->alias_name) ||
            structs.contains(alias->alias_name)) {
          report(*alias, "duplicate type name '" + alias->alias_name + "'");
        } else {
          aliases.emplace(alias->alias_name, alias);
        }
      } else if (const auto *function_alias =
                     dynamic_cast<const FnTypeDef *>(decl)) {
        if (aliases.contains(function_alias->name) ||
            function_aliases.contains(function_alias->name) ||
            structs.contains(function_alias->name)) {
          report(*function_alias,
                 "duplicate type name '" + function_alias->name + "'");
        } else {
          function_aliases.emplace(function_alias->name, function_alias);
        }
      }
    }

    for (const auto &[name, alias] : aliases) {
      (void)name;
      resolve_type(alias->target_type);
    }
    for (const auto &[name, alias] : function_aliases) {
      (void)name;
      function_signature(alias->return_type, alias->params);
      check_function_signature(alias->return_type, alias->params, *alias);
    }
  }

  void collect_functions(const Program &program) {
    for (const TopDecl *decl : program.decls) {
      const auto *function_decl = dynamic_cast<const FnDecl *>(decl);
      const auto *function_def = dynamic_cast<const FnDef *>(decl);
      if (function_decl == nullptr && function_def == nullptr) {
        continue;
      }

      const std::string &name =
          function_decl != nullptr ? function_decl->name : function_def->name;
      TypeInfo signature =
          function_decl != nullptr
              ? function_signature(function_decl->return_type,
                                   function_decl->params)
              : function_signature(function_def->return_type,
                                   function_def->params);
      check_function_signature(
          function_decl != nullptr ? function_decl->return_type
                                   : function_def->return_type,
          function_decl != nullptr ? function_decl->params
                                   : function_def->params,
          *decl);
      auto [it, inserted] =
          functions.emplace(name, FunctionInfo{signature, false});
      if (!inserted && !same_type(it->second.signature, signature)) {
        report(*decl, "conflicting declaration of function '" + name + "'");
      }
      if (function_def != nullptr) {
        if (it->second.defined) {
          report(*function_def,
                 "duplicate definition of function '" + name + "'");
        } else {
          it->second.defined = true;
          it->second.signature = signature;
        }
      }
    }
  }

  bool check_main(const Program &program) {
    const auto main = functions.find("main");
    if (main == functions.end() || !main->second.defined) {
      driver.report_error(SourceLocation{1, 1},
                          "program must define int main() with no arguments");
      return false;
    }
    const TypeInfo &signature = main->second.signature;
    if (!signature.result || !is_int(*signature.result) ||
        !signature.parameters.empty()) {
      const auto definition = std::find_if(
          program.decls.begin(), program.decls.end(), [](const TopDecl *decl) {
            const auto *function = dynamic_cast<const FnDef *>(decl);
            return function != nullptr && function->name == "main";
          });
      if (definition != program.decls.end()) {
        report(**definition, "main must have type int main()");
      }
      return false;
    }
    return true;
  }

  bool check(const Program &program) {
    const size_t initial_errors = driver.error_count();
    aliases.clear();
    function_aliases.clear();
    structs.clear();
    functions.clear();
    scopes.clear();
    scope_floor = 0;
    loop_depth = 0;
    resolving_aliases.clear();

    bool saw_non_use_declaration = false;
    for (const TopDecl *decl : program.decls) {
      if (dynamic_cast<const UseDirective *>(decl) != nullptr) {
        if (saw_non_use_declaration) {
          report(*decl, "#use directives must precede all other declarations");
        }
      } else {
        saw_non_use_declaration = true;
      }
    }

    collect_types(program);
    collect_functions(program);
    check_main(program);

    for (const TopDecl *decl : program.decls) {
      if (const auto *definition = dynamic_cast<const FnDef *>(decl)) {
        check_function(*definition);
      }
    }
    return driver.error_count() == initial_errors;
  }

  void check_function(const FnDef &function) {
    scopes.emplace_back();
    const TypeInfo signature =
        function_signature(function.return_type, function.params);
    current_return_type =
        signature.result ? *signature.result : invalid_type();
    checking_function = true;
    loop_depth = 0;
    scope_floor = scopes.size() - 1;

    for (size_t index = 0; index < function.params.size(); ++index) {
      const ParamDecl *param = function.params[index];
      declare_variable(param->name, signature.parameters[index], *param);
    }

    const bool must_return = check_block(*function.body, false);
    if (!is_void(current_return_type) && !must_return) {
      report(function, "not all control paths in non-void function '" +
                           function.name + "' return a value");
    }

    scopes.pop_back();
    scope_floor = 0;
    checking_function = false;
  }

  bool check_block(const BlockStmt &block, bool create_scope = true) {
    if (create_scope) {
      scopes.emplace_back();
    }
    bool must_return = false;
    for (const Stmt *statement : block.stmts) {
      if (statement == nullptr) {
        continue;
      }
      const bool statement_returns = check_statement(*statement);
      must_return = must_return || statement_returns;
    }
    if (create_scope) {
      scopes.pop_back();
    }
    return must_return;
  }

  bool check_statement(const Stmt &statement) {
    if (const auto *block = dynamic_cast<const BlockStmt *>(&statement)) {
      return check_block(*block);
    }
    if (const auto *declaration =
            dynamic_cast<const VarDeclStmt *>(&statement)) {
      const TypeInfo type = resolve_type(declaration->type);
      if (is_large_type(type)) {
        report(*declaration, "variables cannot have a struct type");
      }
      if (type.valid && type.kind == TypeKind::FunctionPointer) {
        report(*declaration,
               "variables cannot have a function type; use a pointer to a "
               "function type");
      }
      declare_variable(declaration->name, type, *declaration);
      if (declaration->init != nullptr) {
        const TypeInfo value = check_expression(*declaration->init);
        if (!compatible(type, value)) {
          report(*declaration, "initializer type does not match variable '" +
                                   declaration->name + "'");
        }
      }
      return false;
    }
    if (const auto *assignment =
            dynamic_cast<const AssignStmt *>(&statement)) {
      const TypeInfo target = check_expression(*assignment->lhs);
      const TypeInfo value = check_expression(*assignment->rhs);
      if (!is_lvalue(assignment->lhs)) {
        report(*assignment, "assignment target is not an lvalue");
      }
      if (assignment->op == AssignOp::ASSIGN) {
        if (is_large_type(target) || is_large_type(value)) {
          report(*assignment, "struct values cannot be assigned");
        }
        if (!compatible(target, value)) {
          report(*assignment, "assignment types do not match");
        }
      } else if (!is_int(target) || !is_int(value)) {
        report(*assignment, "compound assignment requires int operands");
      }
      return false;
    }
    if (const auto *inc_dec =
            dynamic_cast<const IncDecStmt *>(&statement)) {
      const TypeInfo target = check_expression(*inc_dec->target);
      if (!is_lvalue(inc_dec->target) || !is_int(target)) {
        report(*inc_dec, "increment and decrement require an int lvalue");
      }
      return false;
    }
    if (const auto *expression =
            dynamic_cast<const ExprStmt *>(&statement)) {
      check_expression(*expression->expr);
      return false;
    }
    if (const auto *conditional =
            dynamic_cast<const IfStmt *>(&statement)) {
      const TypeInfo condition = check_expression(*conditional->cond);
      if (!is_bool(condition)) {
        report(*conditional->cond, "if condition must have type bool");
      }
      const bool then_returns = check_statement(*conditional->then_branch);
      const bool else_returns =
          conditional->else_branch != nullptr &&
          check_statement(*conditional->else_branch);
      return conditional->else_branch != nullptr && then_returns &&
             else_returns;
    }
    if (const auto *loop = dynamic_cast<const WhileStmt *>(&statement)) {
      scopes.emplace_back();
      const TypeInfo condition = check_expression(*loop->cond);
      if (!is_bool(condition)) {
        report(*loop->cond, "while condition must have type bool");
      }
      ++loop_depth;
      check_statement(*loop->body);
      --loop_depth;
      scopes.pop_back();
      return false;
    }
    if (const auto *loop = dynamic_cast<const ForStmt *>(&statement)) {
      scopes.emplace_back();
      if (loop->init != nullptr) {
        check_statement(*loop->init);
      }
      const TypeInfo condition = check_expression(*loop->cond);
      if (!is_bool(condition)) {
        report(*loop->cond, "for condition must have type bool");
      }
      if (loop->step != nullptr) {
        check_statement(*loop->step);
      }
      ++loop_depth;
      check_statement(*loop->body);
      --loop_depth;
      scopes.pop_back();
      return false;
    }
    if (const auto *ret = dynamic_cast<const ReturnStmt *>(&statement)) {
      if (!checking_function) {
        report(*ret, "return statement is not inside a function");
        return false;
      }
      if (ret->val == nullptr) {
        if (!is_void(current_return_type)) {
          report(*ret, "non-void function must return a value");
          return false;
        }
      } else {
        const TypeInfo value = check_expression(*ret->val);
        if (is_void(current_return_type) ||
            !compatible(current_return_type, value)) {
          report(*ret, "return value type does not match function return type");
          return false;
        }
      }
      return true;
    }
    if (const auto *assertion =
            dynamic_cast<const AssertStmt *>(&statement)) {
      const TypeInfo condition = check_expression(*assertion->cond);
      if (!is_bool(condition)) {
        report(*assertion->cond, "assert condition must have type bool");
      }
      return false;
    }
    if (const auto *error = dynamic_cast<const ErrorStmt *>(&statement)) {
      const TypeInfo message = check_expression(*error->msg);
      if (message.valid && message.kind != TypeKind::String) {
        report(*error->msg, "error message must have type string");
      }
      return true;
    }
    if (dynamic_cast<const BreakStmt *>(&statement) != nullptr) {
      if (loop_depth == 0) {
        report(statement, "break statement must be inside a loop");
      }
      return false;
    }
    if (dynamic_cast<const ContinueStmt *>(&statement) != nullptr) {
      if (loop_depth == 0) {
        report(statement, "continue statement must be inside a loop");
      }
      return false;
    }
    return false;
  }

  TypeInfo check_expression(const Exp &expression) {
    if (dynamic_cast<const IntLitExp *>(&expression) != nullptr) {
      return primitive(TypeKind::Int);
    }
    if (dynamic_cast<const BoolLitExp *>(&expression) != nullptr) {
      return primitive(TypeKind::Bool);
    }
    if (dynamic_cast<const CharLitExp *>(&expression) != nullptr) {
      return primitive(TypeKind::Char);
    }
    if (dynamic_cast<const StringLitExp *>(&expression) != nullptr) {
      return primitive(TypeKind::String);
    }
    if (dynamic_cast<const NullLitExp *>(&expression) != nullptr) {
      return null_type();
    }
    if (const auto *variable = dynamic_cast<const VarExp *>(&expression)) {
      const auto local = lookup_variable(variable->name);
      if (local.has_value()) {
        return *local;
      }
      const auto function = functions.find(variable->name);
      if (function != functions.end()) {
        return function->second.signature;
      }
      report(*variable, "undeclared identifier '" + variable->name + "'");
      return invalid_type();
    }
    if (const auto *binary = dynamic_cast<const BinaryExp *>(&expression)) {
      const TypeInfo lhs = check_expression(*binary->lhs);
      const TypeInfo rhs = check_expression(*binary->rhs);
      switch (binary->op) {
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
        if (!is_int(lhs) || !is_int(rhs)) {
          report(*binary, "arithmetic and bitwise operators require int operands");
          return invalid_type();
        }
        return primitive(TypeKind::Int);
      case BinaryOp::LT:
      case BinaryOp::LE:
      case BinaryOp::GT:
      case BinaryOp::GE:
        if (!(is_int(lhs) && is_int(rhs)) &&
            !(is_char(lhs) && is_char(rhs))) {
          report(*binary,
                 "relational operators require matching int or char operands");
        }
        return primitive(TypeKind::Bool);
      case BinaryOp::EQ:
      case BinaryOp::NE:
        if (lhs.kind == TypeKind::String || rhs.kind == TypeKind::String ||
            is_large_type(lhs) || is_large_type(rhs)) {
          report(*binary, "equality operators do not support string or struct operands");
        } else if (!(lhs.is_null && rhs.is_null) && !compatible(lhs, rhs) &&
                   !compatible(rhs, lhs)) {
          report(*binary, "equality operands have incompatible types");
        }
        return primitive(TypeKind::Bool);
      case BinaryOp::LOG_AND:
      case BinaryOp::LOG_OR:
        if (!is_bool(lhs) || !is_bool(rhs)) {
          report(*binary, "logical operators require bool operands");
        }
        return primitive(TypeKind::Bool);
      }
    }
    if (const auto *unary = dynamic_cast<const UnaryExp *>(&expression)) {
      if (unary->op == UnaryOp::ADDR_OF) {
        if (const auto *function = dynamic_cast<const VarExp *>(unary->operand)) {
          if (!lookup_variable(function->name).has_value()) {
            const auto found = functions.find(function->name);
            if (found != functions.end()) {
              if (!found->second.defined) {
                report(*unary, "cannot take the address of undefined function '" +
                                   function->name + "'");
                return invalid_type();
              }
              return pointer_to(found->second.signature);
            }
          }
          report(*unary, "address-of operator requires a defined function identifier");
          return invalid_type();
        }
        check_expression(*unary->operand);
        report(*unary, "address-of operator requires a defined function identifier");
        return invalid_type();
      }

      const TypeInfo operand = check_expression(*unary->operand);
      switch (unary->op) {
      case UnaryOp::LOG_NOT:
        if (!is_bool(operand)) {
          report(*unary, "logical not requires a bool operand");
        }
        return primitive(TypeKind::Bool);
      case UnaryOp::BIT_NOT:
      case UnaryOp::NEG:
        if (!is_int(operand)) {
          report(*unary, "numeric unary operator requires an int operand");
        }
        return primitive(TypeKind::Int);
      case UnaryOp::DEREF:
        if (operand.valid && operand.kind == TypeKind::Pointer &&
            operand.element) {
          return *operand.element;
        }
        report(*unary, "dereference requires a pointer operand");
        return invalid_type();
      case UnaryOp::ADDR_OF:
        break;
      }
    }
    if (const auto *ternary =
            dynamic_cast<const TernaryExp *>(&expression)) {
      const TypeInfo condition = check_expression(*ternary->cond);
      const TypeInfo true_type = check_expression(*ternary->true_branch);
      const TypeInfo false_type = check_expression(*ternary->false_branch);
      if (!is_bool(condition)) {
        report(*ternary->cond, "conditional expression condition must be bool");
      }
      if (compatible(true_type, false_type)) {
        return true_type.is_null ? false_type : true_type;
      }
      if (compatible(false_type, true_type)) {
        return false_type.is_null ? true_type : false_type;
      }
      report(*ternary, "conditional expression branches have incompatible types");
      return invalid_type();
    }
    if (const auto *call = dynamic_cast<const CallExp *>(&expression)) {
      const TypeInfo callee = check_expression(*call->callee);
      const TypeInfo *signature = nullptr;
      if (callee.valid && callee.kind == TypeKind::FunctionPointer) {
        signature = &callee;
      } else if (callee.valid && callee.kind == TypeKind::Pointer &&
                 callee.element &&
                 callee.element->kind == TypeKind::FunctionPointer) {
        signature = callee.element.get();
      }

      std::vector<TypeInfo> arguments;
      arguments.reserve(call->args.size());
      for (const Exp *argument : call->args) {
        arguments.push_back(check_expression(*argument));
      }
      if (signature == nullptr) {
        report(*call, "called expression is not a function or function pointer");
        return invalid_type();
      }
      if (arguments.size() != signature->parameters.size()) {
        report(*call, "function called with the wrong number of arguments");
      }
      const size_t count =
          std::min(arguments.size(), signature->parameters.size());
      for (size_t index = 0; index < count; ++index) {
        if (!compatible(signature->parameters[index], arguments[index])) {
          report(*call->args[index],
                 "argument type does not match function parameter");
        }
      }
      return signature->result ? *signature->result : invalid_type();
    }
    if (const auto *field = dynamic_cast<const FieldExp *>(&expression)) {
      TypeInfo base = check_expression(*field->base);
      if (field->is_arrow) {
        if (!base.valid || base.kind != TypeKind::Pointer || !base.element) {
          report(*field, "arrow operator requires a pointer to a struct");
          return invalid_type();
        }
        base = *base.element;
      }
      if (!base.valid || base.kind != TypeKind::Struct) {
        report(*field, "field access requires a struct value");
        return invalid_type();
      }
      const auto struct_info = structs.find(base.name);
      if (struct_info == structs.end() || !struct_info->second.defined) {
        report(*field, "field access on incomplete struct '" + base.name + "'");
        return invalid_type();
      }
      const auto member = struct_info->second.fields.find(field->field_name);
      if (member == struct_info->second.fields.end()) {
        report(*field, "struct '" + base.name + "' has no field named '" +
                           field->field_name + "'");
        return invalid_type();
      }
      return member->second;
    }
    if (const auto *index = dynamic_cast<const IndexExp *>(&expression)) {
      const TypeInfo base = check_expression(*index->base);
      const TypeInfo subscript = check_expression(*index->index);
      if (!is_int(subscript)) {
        report(*index->index, "array index must have type int");
      }
      if (base.valid && base.kind == TypeKind::Array && base.element) {
        if (is_void(*base.element)) {
          report(*index, "cannot index a void array");
          return invalid_type();
        }
        return *base.element;
      }
      report(*index, "indexing requires an array");
      return invalid_type();
    }
    if (const auto *allocation =
            dynamic_cast<const AllocExp *>(&expression)) {
      const TypeInfo allocated = resolve_type(allocation->alloc_type);
      if (is_void(allocated)) {
        report(*allocation, "cannot allocate void");
        return invalid_type();
      }
      if (is_undefined_struct(allocated)) {
        report(*allocation, "cannot allocate an undefined struct");
        return invalid_type();
      }
      return pointer_to(allocated);
    }
    if (const auto *allocation =
            dynamic_cast<const AllocArrayExp *>(&expression)) {
      const TypeInfo count = check_expression(*allocation->count);
      if (!is_int(count)) {
        report(*allocation->count, "array allocation count must have type int");
      }
      const TypeInfo element = resolve_type(allocation->alloc_type);
      if (is_void(element)) {
        report(*allocation, "cannot allocate an array of void");
        return invalid_type();
      }
      if (is_undefined_struct(element)) {
        report(*allocation, "cannot allocate an undefined struct");
        return invalid_type();
      }
      TypeInfo array;
      array.kind = TypeKind::Array;
      array.element = std::make_shared<TypeInfo>(element);
      array.valid = array.element->valid;
      return array;
    }
    if (const auto *lambda = dynamic_cast<const LambdaExp *>(&expression)) {
      TypeInfo signature = function_signature(lambda->return_type, lambda->params);
      check_function_signature(lambda->return_type, lambda->params, *lambda);
      const size_t previous_floor = scope_floor;
      scopes.emplace_back();
      scope_floor = scopes.size() - 1;
      const TypeInfo previous_return_type = current_return_type;
      const bool previous_checking_function = checking_function;
      const size_t previous_loop_depth = loop_depth;
      current_return_type = signature.result ? *signature.result : invalid_type();
      checking_function = true;
      loop_depth = 0;
      for (size_t index = 0; index < lambda->params.size(); ++index) {
        declare_variable(lambda->params[index]->name, signature.parameters[index],
                         *lambda->params[index]);
      }
      if (!is_void(current_return_type) && !check_block(*lambda->body, false)) {
        report(*lambda, "not all control paths in non-void lambda return a value");
      } else if (is_void(current_return_type)) {
        check_block(*lambda->body, false);
      }
      scopes.pop_back();
      scope_floor = previous_floor;
      current_return_type = previous_return_type;
      checking_function = previous_checking_function;
      loop_depth = previous_loop_depth;
      return pointer_to(signature);
    }

    report(expression, "unsupported expression");
    return invalid_type();
  }
};

TypeChecker::TypeChecker(Driver &driver) : impl_(std::make_unique<Impl>(driver)) {}

TypeChecker::~TypeChecker() = default;

bool TypeChecker::check(const Program &program) {
  return impl_->check(program);
}

} // namespace c0
