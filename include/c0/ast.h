#pragma once

#include "c0/memory/symbol_table.h"
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace c0 {

inline constexpr uint32_t kInvalidSlot = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t kInvalidFieldIndex = std::numeric_limits<uint32_t>::max();

struct SemType;
struct StructDesc;
class FnDecl;
class FnDef;
class LambdaExp;

enum class ASTKind : uint8_t {
  Type,
  IntLitExp,
  BoolLitExp,
  CharLitExp,
  StringLitExp,
  NullLitExp,
  VarExp,
  BinaryExp,
  UnaryExp,
  TernaryExp,
  CallExp,
  FieldExp,
  IndexExp,
  AllocExp,
  AllocArrayExp,
  LambdaExp,
  BlockStmt,
  ExprStmt,
  AssignStmt,
  IncDecStmt,
  VarDeclStmt,
  IfStmt,
  WhileStmt,
  ForStmt,
  ReturnStmt,
  AssertStmt,
  ErrorStmt,
  BreakStmt,
  ContinueStmt,
  ParamDecl,
  FieldDecl,
  FnTypeDef,
  StructDecl,
  StructDef,
  FnDecl,
  FnDef,
  TypeDef,
  UseDirective,
  Program
};

enum class SymbolKind : uint8_t { Unresolved = 0, LocalVar, Param, Function };
enum class CallKind : uint8_t {
  Unresolved = 0,
  DirectFn,
  IndirectFnPtr,
  InlineLambda
};

struct SourceLocation {
  int line{1};
  int col{1};
};

struct SourceRange {
  SourceLocation start;
  SourceLocation end;
};

enum class AssignOp {
  ASSIGN,     // =
  ADD_ASSIGN, // +=
  SUB_ASSIGN, // -=
  MUL_ASSIGN, // *=
  DIV_ASSIGN, // /=
  MOD_ASSIGN, // %=
  AND_ASSIGN, // &=
  XOR_ASSIGN, // ^=
  OR_ASSIGN,  // |=
  SHL_ASSIGN, // <<=
  SHR_ASSIGN  // >>=
};

std::string_view to_string(AssignOp op);

enum class BinaryOp {
  ADD,
  SUB,
  MUL,
  DIV,
  MOD,
  SHL,
  SHR,
  LT,
  LE,
  GT,
  GE,
  EQ,
  NE,
  BIT_AND,
  BIT_XOR,
  BIT_OR,
  LOG_AND,
  LOG_OR
};

std::string_view to_string(BinaryOp op);

enum class UnaryOp {
  LOG_NOT, // !
  BIT_NOT, // ~
  NEG,     // -
  DEREF,   // *
  ADDR_OF  // & (C1)
};

std::string_view to_string(UnaryOp op);

// Base AST Node
class ASTNode {
public:
  SourceRange range;
  ASTKind ast_kind;

  explicit ASTNode(ASTKind k, SourceRange r = {}) : range(r), ast_kind(k) {}
  ~ASTNode() = default;
  virtual void dump(std::ostream &os, int indent = 0) const = 0;

protected:
  static void print_indent(std::ostream &os, int indent);
};

enum class TypeKind {
  Int,
  Bool,
  Char,
  String,
  Void,
  Pointer,
  FunctionPointer,
  Array,
  Struct,
  Alias
};

class Type : public ASTNode {
public:
  TypeKind kind;
  Type *elem_type{nullptr};
  std::string name; // For Struct or Alias

  explicit Type(TypeKind k, SourceRange r = {})
      : ASTNode(ASTKind::Type, r), kind(k) {}
  Type(TypeKind k, Type *elem, SourceRange r = {})
      : ASTNode(ASTKind::Type, r), kind(k), elem_type(elem) {}
  Type(TypeKind k, std::string n, SourceRange r = {})
      : ASTNode(ASTKind::Type, r), kind(k), name(std::move(n)) {}

  void dump(std::ostream &os, int indent = 0) const override;
};

// Expressions
class Exp : public ASTNode {
public:
  const SemType *sem_type{nullptr};

  explicit Exp(ASTKind k, SourceRange r = {}) : ASTNode(k, r) {}
  virtual bool is_lvalue() const { return false; }
};

class ParamDecl;
class BlockStmt;

class IntLitExp : public Exp {
public:
  int32_t val;
  IntLitExp(int32_t v, SourceRange r = {})
      : Exp(ASTKind::IntLitExp, r), val(v) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class BoolLitExp : public Exp {
public:
  bool val;
  BoolLitExp(bool v, SourceRange r = {})
      : Exp(ASTKind::BoolLitExp, r), val(v) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class CharLitExp : public Exp {
public:
  char val;
  CharLitExp(char v, SourceRange r = {})
      : Exp(ASTKind::CharLitExp, r), val(v) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class StringLitExp : public Exp {
public:
  std::string val;
  StringLitExp(std::string v, SourceRange r = {})
      : Exp(ASTKind::StringLitExp, r), val(std::move(v)) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class NullLitExp : public Exp {
public:
  explicit NullLitExp(SourceRange r = {}) : Exp(ASTKind::NullLitExp, r) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class VarExp : public Exp {
public:
  std::string name;
  SymbolId symbol_id{kInvalidSymbol};
  uint32_t slot_id{kInvalidSlot};
  SymbolKind symbol_kind{SymbolKind::Unresolved};
  const FnDef *resolved_fn_def{nullptr};
  const FnDecl *resolved_fn_decl{nullptr};

  VarExp(std::string n, SourceRange r = {})
      : Exp(ASTKind::VarExp, r), name(std::move(n)) {}
  bool is_lvalue() const override { return true; }
  void dump(std::ostream &os, int indent = 0) const override;
};

class BinaryExp : public Exp {
public:
  BinaryOp op;
  Exp *lhs;
  Exp *rhs;
  BinaryExp(BinaryOp o, Exp *l, Exp *r, SourceRange rng = {})
      : Exp(ASTKind::BinaryExp, rng), op(o), lhs(l), rhs(r) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class UnaryExp : public Exp {
public:
  UnaryOp op;
  Exp *operand;
  UnaryExp(UnaryOp o, Exp *opnd, SourceRange rng = {})
      : Exp(ASTKind::UnaryExp, rng), op(o), operand(opnd) {}
  bool is_lvalue() const override { return op == UnaryOp::DEREF; }
  void dump(std::ostream &os, int indent = 0) const override;
};

class TernaryExp : public Exp {
public:
  Exp *cond;
  Exp *true_branch;
  Exp *false_branch;
  TernaryExp(Exp *c, Exp *t, Exp *f, SourceRange r = {})
      : Exp(ASTKind::TernaryExp, r), cond(c), true_branch(t), false_branch(f) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class CallExp : public Exp {
public:
  Exp *callee;
  std::vector<Exp *> args;
  CallKind call_kind{CallKind::Unresolved};
  SymbolId callee_symbol{kInvalidSymbol};
  const FnDef *resolved_fn_def{nullptr};
  const FnDecl *resolved_fn_decl{nullptr};
  const LambdaExp *resolved_lambda{nullptr};

  CallExp(Exp *c, std::vector<Exp *> a, SourceRange r = {})
      : Exp(ASTKind::CallExp, r), callee(c), args(std::move(a)) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class FieldExp : public Exp {
public:
  Exp *base;
  std::string field_name;
  bool is_arrow; // true for ->, false for .
  const StructDesc *struct_desc{nullptr};
  uint32_t field_index{kInvalidFieldIndex};

  FieldExp(Exp *b, std::string f, bool arrow, SourceRange r = {})
      : Exp(ASTKind::FieldExp, r), base(b), field_name(std::move(f)),
        is_arrow(arrow) {}
  bool is_lvalue() const override { return true; }
  void dump(std::ostream &os, int indent = 0) const override;
};

class IndexExp : public Exp {
public:
  Exp *base;
  Exp *index;
  IndexExp(Exp *b, Exp *i, SourceRange r = {})
      : Exp(ASTKind::IndexExp, r), base(b), index(i) {}
  bool is_lvalue() const override { return true; }
  void dump(std::ostream &os, int indent = 0) const override;
};

class AllocExp : public Exp {
public:
  Type *alloc_type;
  AllocExp(Type *t, SourceRange r = {})
      : Exp(ASTKind::AllocExp, r), alloc_type(t) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class AllocArrayExp : public Exp {
public:
  Type *alloc_type;
  Exp *count;
  AllocArrayExp(Type *t, Exp *c, SourceRange r = {})
      : Exp(ASTKind::AllocArrayExp, r), alloc_type(t), count(c) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class LambdaExp : public Exp {
public:
  Type *return_type;
  std::vector<ParamDecl *> params;
  BlockStmt *body;
  SymbolId synthesized_name{kInvalidSymbol};
  const SemType *fn_sig{nullptr};
  uint32_t local_count{0};

  LambdaExp(Type *ret, std::vector<ParamDecl *> p, BlockStmt *b, SourceRange r = {})
      : Exp(ASTKind::LambdaExp, r), return_type(ret), params(std::move(p)),
        body(b) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

// Statements
class Stmt : public ASTNode {
public:
  explicit Stmt(ASTKind k, SourceRange r = {}) : ASTNode(k, r) {}
};

class BlockStmt : public Stmt {
public:
  std::vector<Stmt *> stmts;
  BlockStmt(std::vector<Stmt *> s, SourceRange r = {})
      : Stmt(ASTKind::BlockStmt, r), stmts(std::move(s)) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class ExprStmt : public Stmt {
public:
  Exp *expr;
  ExprStmt(Exp *e, SourceRange r = {})
      : Stmt(ASTKind::ExprStmt, r), expr(e) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class AssignStmt : public Stmt {
public:
  Exp *lhs;
  AssignOp op;
  Exp *rhs;
  uint32_t lhs_slot_id{kInvalidSlot};
  SymbolKind lhs_symbol_kind{SymbolKind::Unresolved};

  AssignStmt(Exp *l, AssignOp o, Exp *r, SourceRange rng = {})
      : Stmt(ASTKind::AssignStmt, rng), lhs(l), op(o), rhs(r) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class IncDecStmt : public Stmt {
public:
  Exp *target;
  bool is_inc; // true for ++, false for --
  IncDecStmt(Exp *t, bool inc, SourceRange r = {})
      : Stmt(ASTKind::IncDecStmt, r), target(t), is_inc(inc) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class VarDeclStmt : public Stmt {
public:
  Type *type;
  std::string name;
  Exp *init; // null if uninitialized
  SymbolId symbol_id{kInvalidSymbol};
  uint32_t slot_id{kInvalidSlot};
  const SemType *sem_type{nullptr};

  VarDeclStmt(Type *t, std::string n, Exp *i, SourceRange r = {})
      : Stmt(ASTKind::VarDeclStmt, r), type(t), name(std::move(n)), init(i) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class IfStmt : public Stmt {
public:
  Exp *cond;
  Stmt *then_branch;
  Stmt *else_branch; // nullable
  IfStmt(Exp *c, Stmt *t, Stmt *e = nullptr, SourceRange r = {})
      : Stmt(ASTKind::IfStmt, r), cond(c), then_branch(t), else_branch(e) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class WhileStmt : public Stmt {
public:
  Exp *cond;
  Stmt *body;
  WhileStmt(Exp *c, Stmt *b, SourceRange r = {})
      : Stmt(ASTKind::WhileStmt, r), cond(c), body(b) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class ForStmt : public Stmt {
public:
  Stmt *init; // nullable
  Exp *cond;
  Stmt *step; // nullable
  Stmt *body;
  ForStmt(Stmt *i, Exp *c, Stmt *s, Stmt *b, SourceRange r = {})
      : Stmt(ASTKind::ForStmt, r), init(i), cond(c), step(s), body(b) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class ReturnStmt : public Stmt {
public:
  Exp *val; // nullable
  ReturnStmt(Exp *v = nullptr, SourceRange r = {})
      : Stmt(ASTKind::ReturnStmt, r), val(v) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class AssertStmt : public Stmt {
public:
  Exp *cond;
  AssertStmt(Exp *c, SourceRange r = {})
      : Stmt(ASTKind::AssertStmt, r), cond(c) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class ErrorStmt : public Stmt {
public:
  Exp *msg;
  ErrorStmt(Exp *m, SourceRange r = {})
      : Stmt(ASTKind::ErrorStmt, r), msg(m) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class BreakStmt : public Stmt {
public:
  explicit BreakStmt(SourceRange r = {}) : Stmt(ASTKind::BreakStmt, r) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class ContinueStmt : public Stmt {
public:
  explicit ContinueStmt(SourceRange r = {}) : Stmt(ASTKind::ContinueStmt, r) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

// Declarations & Definitions
class TopDecl : public ASTNode {
public:
  explicit TopDecl(ASTKind k, SourceRange r = {}) : ASTNode(k, r) {}
};

class ParamDecl : public ASTNode {
public:
  Type *type;
  std::string name;
  SymbolId symbol_id{kInvalidSymbol};
  uint32_t slot_id{kInvalidSlot};
  const SemType *sem_type{nullptr};

  ParamDecl(Type *t, std::string n, SourceRange r = {})
      : ASTNode(ASTKind::ParamDecl, r), type(t), name(std::move(n)) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class FieldDecl : public ASTNode {
public:
  Type *type;
  std::string name;
  SymbolId symbol_id{kInvalidSymbol};
  uint32_t field_index{kInvalidFieldIndex};
  const SemType *sem_type{nullptr};

  FieldDecl(Type *t, std::string n, SourceRange r = {})
      : ASTNode(ASTKind::FieldDecl, r), type(t), name(std::move(n)) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class FnTypeDef : public TopDecl {
public:
  Type *return_type;
  std::string name;
  std::vector<ParamDecl *> params;

  FnTypeDef(Type *ret, std::string n, std::vector<ParamDecl *> p,
            SourceRange r = {})
      : TopDecl(ASTKind::FnTypeDef, r), return_type(ret), name(std::move(n)),
        params(std::move(p)) {}

  void dump(std::ostream &os, int indent = 0) const override;
};

class StructDecl : public TopDecl {
public:
  std::string name;
  StructDecl(std::string n, SourceRange r = {})
      : TopDecl(ASTKind::StructDecl, r), name(std::move(n)) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class StructDef : public TopDecl {
public:
  std::string name;
  std::vector<FieldDecl *> fields;
  SymbolId symbol_id{kInvalidSymbol};
  const StructDesc *struct_desc{nullptr};

  StructDef(std::string n, std::vector<FieldDecl *> f, SourceRange r = {})
      : TopDecl(ASTKind::StructDef, r), name(std::move(n)),
        fields(std::move(f)) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class FnDecl : public TopDecl {
public:
  Type *return_type;
  std::string name;
  std::vector<ParamDecl *> params;
  SymbolId symbol_id{kInvalidSymbol};
  const SemType *fn_sig{nullptr};

  FnDecl(Type *t, std::string n, std::vector<ParamDecl *> p, SourceRange r = {})
      : TopDecl(ASTKind::FnDecl, r), return_type(t), name(std::move(n)),
        params(std::move(p)) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class FnDef : public TopDecl {
public:
  Type *return_type;
  std::string name;
  std::vector<ParamDecl *> params;
  BlockStmt *body;
  SymbolId symbol_id{kInvalidSymbol};
  const SemType *fn_sig{nullptr};
  uint32_t local_count{0};

  FnDef(Type *t, std::string n, std::vector<ParamDecl *> p, BlockStmt *b,
        SourceRange r = {})
      : TopDecl(ASTKind::FnDef, r), return_type(t), name(std::move(n)),
        params(std::move(p)), body(b) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class TypeDef : public TopDecl {
public:
  Type *target_type;
  std::string alias_name;
  TypeDef(Type *t, std::string a, SourceRange r = {})
      : TopDecl(ASTKind::TypeDef, r), target_type(t),
        alias_name(std::move(a)) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class UseDirective : public TopDecl {
public:
  std::string path;
  bool is_lib; // true for <lib>, false for "file"
  UseDirective(std::string p, bool lib, SourceRange r = {})
      : TopDecl(ASTKind::UseDirective, r), path(std::move(p)), is_lib(lib) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

class Program : public ASTNode {
public:
  std::vector<TopDecl *> decls;
  std::vector<LambdaExp *> hoisted_lambdas;

  Program(std::vector<TopDecl *> d, SourceRange r = {})
      : ASTNode(ASTKind::Program, r), decls(std::move(d)) {}
  void dump(std::ostream &os, int indent = 0) const override;
};

} // namespace c0
