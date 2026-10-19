#include "c0/ast.h"

namespace c0 {

std::string_view to_string(AssignOp op) {
  switch (op) {
  case AssignOp::ASSIGN:
    return "=";
  case AssignOp::ADD_ASSIGN:
    return "+=";
  case AssignOp::SUB_ASSIGN:
    return "-=";
  case AssignOp::MUL_ASSIGN:
    return "*=";
  case AssignOp::DIV_ASSIGN:
    return "/=";
  case AssignOp::MOD_ASSIGN:
    return "%=";
  case AssignOp::AND_ASSIGN:
    return "&=";
  case AssignOp::XOR_ASSIGN:
    return "^=";
  case AssignOp::OR_ASSIGN:
    return "|=";
  case AssignOp::SHL_ASSIGN:
    return "<<=";
  case AssignOp::SHR_ASSIGN:
    return ">>=";
  }
  return "?=";
}

std::string_view to_string(BinaryOp op) {
  switch (op) {
  case BinaryOp::ADD:
    return "+";
  case BinaryOp::SUB:
    return "-";
  case BinaryOp::MUL:
    return "*";
  case BinaryOp::DIV:
    return "/";
  case BinaryOp::MOD:
    return "%";
  case BinaryOp::SHL:
    return "<<";
  case BinaryOp::SHR:
    return ">>";
  case BinaryOp::LT:
    return "<";
  case BinaryOp::LE:
    return "<=";
  case BinaryOp::GT:
    return ">";
  case BinaryOp::GE:
    return ">=";
  case BinaryOp::EQ:
    return "==";
  case BinaryOp::NE:
    return "!=";
  case BinaryOp::BIT_AND:
    return "&";
  case BinaryOp::BIT_XOR:
    return "^";
  case BinaryOp::BIT_OR:
    return "|";
  case BinaryOp::LOG_AND:
    return "&&";
  case BinaryOp::LOG_OR:
    return "||";
  }
  return "?";
}

std::string_view to_string(UnaryOp op) {
  switch (op) {
  case UnaryOp::LOG_NOT:
    return "!";
  case UnaryOp::BIT_NOT:
    return "~";
  case UnaryOp::NEG:
    return "-";
  case UnaryOp::DEREF:
    return "*";
  case UnaryOp::ADDR_OF:
    return "&";
  }
  return "?";
}

void ASTNode::print_indent(std::ostream &os, int indent) {
  for (int i = 0; i < indent; ++i) {
    os << "  ";
  }
}

void Type::dump(std::ostream &os, int indent) const {
  (void)indent;
  switch (kind) {
  case TypeKind::Int:
    os << "int";
    break;
  case TypeKind::Bool:
    os << "bool";
    break;
  case TypeKind::Char:
    os << "char";
    break;
  case TypeKind::String:
    os << "string";
    break;
  case TypeKind::Void:
    os << "void";
    break;
  case TypeKind::Pointer:
    if (elem_type)
      elem_type->dump(os, 0);
    os << "*";
    break;
  case TypeKind::Array:
    if (elem_type)
      elem_type->dump(os, 0);
    os << "[]";
    break;
  case TypeKind::Struct:
    os << "struct " << name;
    break;
  case TypeKind::Alias:
    os << name;
    break;
  }
}

void IntLitExp::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "IntLit: " << val << "\n";
}

void BoolLitExp::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "BoolLit: " << (val ? "true" : "false") << "\n";
}

void CharLitExp::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "CharLit: '" << val << "'\n";
}

void StringLitExp::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "StringLit: \"" << val << "\"\n";
}

void NullLitExp::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "NullLit\n";
}

void VarExp::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "Var: " << name << "\n";
}

void BinaryExp::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "BinaryExp: " << to_string(op) << "\n";
  if (lhs)
    lhs->dump(os, indent + 1);
  if (rhs)
    rhs->dump(os, indent + 1);
}

void UnaryExp::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "UnaryExp: " << to_string(op) << "\n";
  if (operand)
    operand->dump(os, indent + 1);
}

void TernaryExp::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "TernaryExp (? :)\n";
  if (cond)
    cond->dump(os, indent + 1);
  if (true_branch)
    true_branch->dump(os, indent + 1);
  if (false_branch)
    false_branch->dump(os, indent + 1);
}

void CallExp::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "CallExp\n";
  print_indent(os, indent + 1);
  os << "Callee:\n";
  if (callee)
    callee->dump(os, indent + 2);
  print_indent(os, indent + 1);
  os << "Args (" << args.size() << "):\n";
  for (auto *arg : args) {
    if (arg)
      arg->dump(os, indent + 2);
  }
}

void FieldExp::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "FieldExp (" << (is_arrow ? "->" : ".") << field_name << ")\n";
  if (base)
    base->dump(os, indent + 1);
}

void IndexExp::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "IndexExp\n";
  print_indent(os, indent + 1);
  os << "Base:\n";
  if (base)
    base->dump(os, indent + 2);
  print_indent(os, indent + 1);
  os << "Index:\n";
  if (index)
    index->dump(os, indent + 2);
}

void AllocExp::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "Alloc (";
  if (alloc_type)
    alloc_type->dump(os);
  os << ")\n";
}

void AllocArrayExp::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "AllocArray (";
  if (alloc_type)
    alloc_type->dump(os);
  os << ")\n";
  print_indent(os, indent + 1);
  os << "Count:\n";
  if (count)
    count->dump(os, indent + 2);
}

void BlockStmt::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "BlockStmt (" << stmts.size() << " stmts)\n";
  for (auto *s : stmts) {
    if (s)
      s->dump(os, indent + 1);
  }
}

void ExprStmt::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "ExprStmt\n";
  if (expr)
    expr->dump(os, indent + 1);
}

void AssignStmt::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "AssignStmt: " << to_string(op) << "\n";
  if (lhs)
    lhs->dump(os, indent + 1);
  if (rhs)
    rhs->dump(os, indent + 1);
}

void IncDecStmt::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "IncDecStmt: " << (is_inc ? "++" : "--") << "\n";
  if (target)
    target->dump(os, indent + 1);
}

void VarDeclStmt::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "VarDeclStmt: ";
  if (type)
    type->dump(os);
  os << " " << name << "\n";
  if (init) {
    print_indent(os, indent + 1);
    os << "Init:\n";
    init->dump(os, indent + 2);
  }
}

void IfStmt::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "IfStmt\n";
  print_indent(os, indent + 1);
  os << "Cond:\n";
  if (cond)
    cond->dump(os, indent + 2);
  print_indent(os, indent + 1);
  os << "Then:\n";
  if (then_branch)
    then_branch->dump(os, indent + 2);
  if (else_branch) {
    print_indent(os, indent + 1);
    os << "Else:\n";
    else_branch->dump(os, indent + 2);
  }
}

void WhileStmt::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "WhileStmt\n";
  print_indent(os, indent + 1);
  os << "Cond:\n";
  if (cond)
    cond->dump(os, indent + 2);
  print_indent(os, indent + 1);
  os << "Body:\n";
  if (body)
    body->dump(os, indent + 2);
}

void ForStmt::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "ForStmt\n";
  if (init) {
    print_indent(os, indent + 1);
    os << "Init:\n";
    init->dump(os, indent + 2);
  }
  print_indent(os, indent + 1);
  os << "Cond:\n";
  if (cond)
    cond->dump(os, indent + 2);
  if (step) {
    print_indent(os, indent + 1);
    os << "Step:\n";
    step->dump(os, indent + 2);
  }
  print_indent(os, indent + 1);
  os << "Body:\n";
  if (body)
    body->dump(os, indent + 2);
}

void ReturnStmt::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "ReturnStmt\n";
  if (val)
    val->dump(os, indent + 1);
}

void AssertStmt::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "AssertStmt\n";
  if (cond)
    cond->dump(os, indent + 1);
}

void ErrorStmt::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "ErrorStmt\n";
  if (msg)
    msg->dump(os, indent + 1);
}

void BreakStmt::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "BreakStmt\n";
}

void ContinueStmt::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "ContinueStmt\n";
}

void ParamDecl::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "Param: ";
  if (type)
    type->dump(os);
  os << " " << name << "\n";
}

void FieldDecl::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "Field: ";
  if (type)
    type->dump(os);
  os << " " << name << "\n";
}

void FnTypeDef::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "FnTypeDef: ";
  if (return_type)
    return_type->dump(os);
  os << " " << name << "(" << params.size() << " params)\n";
  for (auto *p : params) {
    if (p)
      p->dump(os, indent + 1);
  }
}

void StructDecl::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "StructDecl: " << name << "\n";
}

void StructDef::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "StructDef: " << name << " (" << fields.size() << " fields)\n";
  for (auto *f : fields) {
    if (f)
      f->dump(os, indent + 1);
  }
}

void FnDecl::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "FnDecl: ";
  if (return_type)
    return_type->dump(os);
  os << " " << name << "(" << params.size() << " params)\n";
  for (auto *p : params) {
    if (p)
      p->dump(os, indent + 1);
  }
}

void FnDef::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "FnDef: ";
  if (return_type)
    return_type->dump(os);
  os << " " << name << "(" << params.size() << " params)\n";
  for (auto *p : params) {
    if (p)
      p->dump(os, indent + 1);
  }
  if (body)
    body->dump(os, indent + 1);
}

void TypeDef::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "TypeDef: ";
  if (target_type)
    target_type->dump(os);
  os << " as " << alias_name << "\n";
}

void UseDirective::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "UseDirective: " << (is_lib ? "<" : "\"") << path
     << (is_lib ? ">" : "\"") << "\n";
}

void Program::dump(std::ostream &os, int indent) const {
  print_indent(os, indent);
  os << "Program (" << decls.size() << " top-level declarations)\n";
  for (auto *d : decls) {
    if (d)
      d->dump(os, indent + 1);
  }
}

} // namespace c0
