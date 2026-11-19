#include "c0/ast.h"
#include "c0/driver.h"
#include "c0/arena.h"
#include "c0/memory/symbol_table.h"
#include "c0/typecheck/type_context.h"
#include "c0/typecheck/typechecker.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "CHECK FAILED at " << __FILE__ << ":" << __LINE__ << ": "   \
                << #cond << "\n";                                              \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)

namespace {

void test_symbol_table() {
  c0::Arena arena;
  c0::SymbolTable table(arena);

  const c0::SymbolId a1 = table.intern("alpha");
  const c0::SymbolId b1 = table.intern("beta");
  const c0::SymbolId a2 = table.intern(std::string("alpha"));
  CHECK(a1 == a2);
  CHECK(a1 != b1);
  CHECK(table.find("alpha") == a1);
  CHECK(table.find("beta") == b1);
  CHECK(table.find("missing") == c0::kInvalidSymbol);
  CHECK(table.resolve(a1) == "alpha");
  CHECK(table.resolve(b1) == "beta");

  // Rehash stress across 500 distinct identifiers
  std::vector<c0::SymbolId> ids;
  ids.reserve(500);
  for (int i = 0; i < 500; ++i) {
    ids.push_back(table.intern("sym_" + std::to_string(i)));
  }
  for (int i = 0; i < 500; ++i) {
    const std::string key = "sym_" + std::to_string(i);
    CHECK(table.intern(key) == ids[static_cast<std::size_t>(i)]);
    CHECK(table.find(key) == ids[static_cast<std::size_t>(i)]);
    CHECK(table.resolve(ids[static_cast<std::size_t>(i)]) == key);
  }
}

void test_type_context() {
  c0::Arena arena;
  c0::SymbolTable symbols(arena);
  c0::TypeContext tc(arena);

  CHECK(tc.int_type() == tc.int_type());
  CHECK(tc.int_type() != tc.bool_type());
  CHECK(tc.invalid_type()->is_invalid());
  CHECK(tc.null_type()->is_null());

  // Pointer & array interning yields O(1) pointer identity
  const c0::SemType *int_arr_ptr_1 = tc.get_pointer(tc.get_array(tc.int_type()));
  const c0::SemType *int_arr_ptr_2 = tc.get_pointer(tc.get_array(tc.int_type()));
  CHECK(int_arr_ptr_1 == int_arr_ptr_2);
  CHECK(c0::TypeContext::same_type(int_arr_ptr_1, int_arr_ptr_2));

  // NULL compatibility
  CHECK(c0::TypeContext::compatible(int_arr_ptr_1, tc.null_type()));
  CHECK(!c0::TypeContext::compatible(tc.int_type(), tc.null_type()));
  CHECK(!c0::TypeContext::compatible(tc.string_type(), tc.null_type()));

  // Struct descriptors
  const c0::SymbolId s_sym = symbols.intern("point");
  c0::StructDesc *s_desc = tc.get_or_create_struct_desc(s_sym);
  CHECK(tc.get_struct(s_sym) == tc.get_struct(s_sym));
  CHECK(!s_desc->defined);

  const c0::SymbolId fx = symbols.intern("x");
  const c0::SymbolId fy = symbols.intern("y");
  const c0::StructFieldDesc fields[2] = {
      {fx, tc.int_type(), 0},
      {fy, tc.bool_type(), 1},
  };
  tc.set_struct_fields(s_desc, fields);
  CHECK(s_desc->defined);
  CHECK(s_desc->field_count == 2);
  CHECK(s_desc->find_field(fy) != nullptr);
  CHECK(s_desc->find_field(fy)->index == 1);
  CHECK(s_desc->find_field(symbols.intern("z")) == nullptr);

  // Nominal vs anonymous function pointer identity
  const c0::SymbolId op1 = symbols.intern("op1");
  const c0::SymbolId op2 = symbols.intern("op2");
  const c0::SemType *params[1] = {tc.int_type()};
  const c0::SemType *fn_op1 = tc.get_function(tc.int_type(), params, op1);
  const c0::SemType *fn_op2 = tc.get_function(tc.int_type(), params, op2);
  const c0::SemType *fn_anon = tc.get_function(tc.int_type(), params);

  CHECK(fn_op1 != fn_op2);
  CHECK(!c0::TypeContext::same_type(fn_op1, fn_op2));
  CHECK(c0::TypeContext::same_type(fn_op1, fn_anon));
  CHECK(c0::TypeContext::same_type(tc.get_pointer(fn_op2),
                                   tc.get_pointer(fn_anon)));
  CHECK(!c0::TypeContext::same_type(tc.get_pointer(fn_op1),
                                    tc.get_pointer(fn_op2)));
}

void test_ast_decoration() {
  const char *code = R"c0(
    struct point {
        int a;
        int b;
    };

    typedef int binop(int x, int y);

    int add(int x, int y) {
        return x + y;
    }

    int accept_ptr(struct point* p, int* np) {
        return p == NULL && np != NULL ? 1 : 0;
    }

    int* make_null(bool cond, int* q) {
        if (cond) {
            return q;
        }
        return NULL;
    }

    int main() {
        struct point* pt = alloc(struct point);
        pt->b = add(1, 2);

        binop* fp = &add;
        int r1 = (*fp)(3, 4);
        int r2 = fp(5, 6);

        int r3 = [](int v) -> int {
            binop* inner = [](int a, int b) -> int {
                return a + b;
            };
            return (*inner)(v, 1);
        }(7);

        int* p1 = NULL;
        p1 = NULL;
        int* p2 = make_null(true, NULL);
        int* p3 = (r1 > 0) ? p2 : NULL;
        int* p4 = (r2 > 0) ? NULL : NULL;
        int chk = accept_ptr(NULL, p3);
        if (p1 == NULL && p4 == NULL) {
            return pt->b + r3 + chk;
        }
        return 0;
    }
  )c0";

  c0::Driver driver;
  CHECK(driver.parse_string(code, "decoration_test.c0"));
  CHECK(!driver.has_errors());
  CHECK(driver.ast() != nullptr);

  c0::TypeChecker tc(driver);
  CHECK(tc.check(*driver.ast()));
  CHECK(!driver.has_errors());

  const c0::TypeContext &tctx = driver.type_context();
  const c0::Program *prog = driver.ast();

  // Hoisted lambdas (outer + nested inner)
  CHECK(prog->hoisted_lambdas.size() == 2);
  CHECK(prog->hoisted_lambdas[0]->synthesized_name != c0::kInvalidSymbol);
  CHECK(prog->hoisted_lambdas[1]->synthesized_name != c0::kInvalidSymbol);
  CHECK(prog->hoisted_lambdas[0]->synthesized_name !=
        prog->hoisted_lambdas[1]->synthesized_name);
  CHECK(prog->hoisted_lambdas[0]->local_count >= 2);
  CHECK(prog->hoisted_lambdas[1]->local_count == 2);

  // Locate main() and make_null()
  const c0::FnDef *main_fn = nullptr;
  const c0::FnDef *make_null_fn = nullptr;
  for (const c0::TopDecl *decl : prog->decls) {
    if (decl && decl->ast_kind == c0::ASTKind::FnDef) {
      const auto *fn = static_cast<const c0::FnDef *>(decl);
      if (fn->name == "main") {
        main_fn = fn;
      } else if (fn->name == "make_null") {
        make_null_fn = fn;
      }
    }
  }
  CHECK(main_fn != nullptr);
  CHECK(make_null_fn != nullptr);
  CHECK(main_fn->local_count > 5);

  // Verify ReturnStmt NULL concretization inside make_null
  CHECK(make_null_fn->body->stmts.size() == 2);
  const auto *ret_null =
      static_cast<const c0::ReturnStmt *>(make_null_fn->body->stmts[1]);
  CHECK(ret_null->val->ast_kind == c0::ASTKind::NullLitExp);
  const c0::SemType *int_ptr_expected =
      driver.type_context().get_pointer(tctx.int_type());
  CHECK(ret_null->val->sem_type == int_ptr_expected);

  const auto &stmts = main_fn->body->stmts;
  // Stmt 1: pt->b = add(1, 2);
  const auto *assign_pt_b = static_cast<const c0::AssignStmt *>(stmts[1]);
  const auto *field_b = static_cast<const c0::FieldExp *>(assign_pt_b->lhs);
  CHECK(field_b->field_index == 1);
  CHECK(field_b->struct_desc != nullptr);
  CHECK(field_b->sem_type == tctx.int_type());

  const auto *direct_call = static_cast<const c0::CallExp *>(assign_pt_b->rhs);
  CHECK(direct_call->call_kind == c0::CallKind::DirectFn);
  CHECK(direct_call->resolved_fn_def != nullptr);
  CHECK(direct_call->resolved_fn_def->name == "add");
  CHECK(direct_call->sem_type == tctx.int_type());

  // Stmt 2: binop* fp = &add;
  const auto *fp_decl = static_cast<const c0::VarDeclStmt *>(stmts[2]);
  CHECK(fp_decl->slot_id != c0::kInvalidSlot);
  const auto *addr_add = static_cast<const c0::UnaryExp *>(fp_decl->init);
  CHECK(addr_add->op == c0::UnaryOp::ADDR_OF);
  const auto *add_var = static_cast<const c0::VarExp *>(addr_add->operand);
  CHECK(add_var->symbol_kind == c0::SymbolKind::Function);
  CHECK(add_var->resolved_fn_def != nullptr);

  // Stmts 3 & 4: indirect calls (*fp)(...) and fp(...)
  const auto *r1_decl = static_cast<const c0::VarDeclStmt *>(stmts[3]);
  const auto *indirect_1 = static_cast<const c0::CallExp *>(r1_decl->init);
  CHECK(indirect_1->call_kind == c0::CallKind::IndirectFnPtr);

  const auto *r2_decl = static_cast<const c0::VarDeclStmt *>(stmts[4]);
  const auto *indirect_2 = static_cast<const c0::CallExp *>(r2_decl->init);
  CHECK(indirect_2->call_kind == c0::CallKind::IndirectFnPtr);

  // Stmt 5: inline lambda call
  const auto *r3_decl = static_cast<const c0::VarDeclStmt *>(stmts[5]);
  const auto *inline_lam = static_cast<const c0::CallExp *>(r3_decl->init);
  CHECK(inline_lam->call_kind == c0::CallKind::InlineLambda);
  CHECK(inline_lam->resolved_lambda == prog->hoisted_lambdas[0]);

  // Stmt 6: int* p1 = NULL;
  const auto *p1_decl = static_cast<const c0::VarDeclStmt *>(stmts[6]);
  const c0::SemType *int_ptr =
      driver.type_context().get_pointer(tctx.int_type());
  CHECK(p1_decl->sem_type == int_ptr);
  CHECK(p1_decl->init->sem_type == int_ptr);

  // Stmt 7: p1 = NULL;
  const auto *p1_assign = static_cast<const c0::AssignStmt *>(stmts[7]);
  CHECK(p1_assign->lhs_slot_id == p1_decl->slot_id);
  CHECK(p1_assign->lhs_symbol_kind == c0::SymbolKind::LocalVar);
  CHECK(p1_assign->rhs->sem_type == int_ptr);

  // Stmt 8: make_null(true, NULL) -> argument NULL concretized to int*
  const auto *p2_decl = static_cast<const c0::VarDeclStmt *>(stmts[8]);
  const auto *call_mn = static_cast<const c0::CallExp *>(p2_decl->init);
  CHECK(call_mn->args[1]->sem_type == int_ptr);

  // Stmt 9: ternary (r1 > 0) ? p2 : NULL -> NULL branch concretized to int*
  const auto *p3_decl = static_cast<const c0::VarDeclStmt *>(stmts[9]);
  const auto *tern_p3 = static_cast<const c0::TernaryExp *>(p3_decl->init);
  CHECK(tern_p3->false_branch->sem_type == int_ptr);

  // Stmt 10: ternary (r2 > 0) ? NULL : NULL -> both NULLs concretized to int*
  const auto *p4_decl = static_cast<const c0::VarDeclStmt *>(stmts[10]);
  const auto *tern_p4 = static_cast<const c0::TernaryExp *>(p4_decl->init);
  CHECK(tern_p4->sem_type == int_ptr);
  CHECK(tern_p4->true_branch->sem_type == int_ptr);
  CHECK(tern_p4->false_branch->sem_type == int_ptr);

  // Stmt 12: if (p1 == NULL && p4 == NULL)
  const auto *if_null = static_cast<const c0::IfStmt *>(stmts[12]);
  const auto *and_expr = static_cast<const c0::BinaryExp *>(if_null->cond);
  const auto *eq_left = static_cast<const c0::BinaryExp *>(and_expr->lhs);
  CHECK(eq_left->rhs->sem_type == int_ptr);
}

void test_bitset_overflow_definite_init() {
  auto make_many_locals = [](bool init_in_else) {
    std::string src = "int main() {\n";
    for (int i = 0; i < 75; ++i) {
      if (i == 70) {
        src += "    int v70;\n";
      } else {
        src += "    int v" + std::to_string(i) + " = " + std::to_string(i) +
               ";\n";
      }
    }
    src += "    if (v0 == 0) { v70 = 1; }";
    if (init_in_else) {
      src += " else { v70 = 2; }";
    }
    src += "\n    return v70;\n}\n";
    return src;
  };

  // Valid case: >64 locals and v70 initialized on all paths
  {
    c0::Driver driver;
    CHECK(driver.parse_string(make_many_locals(true), "overflow_valid.c0"));
    c0::TypeChecker tc(driver);
    CHECK(tc.check(*driver.ast()));
    CHECK(!driver.has_errors());
  }

  // Invalid case: >64 locals and v70 uninitialized on false branch
  {
    c0::Driver driver;
    CHECK(driver.parse_string(make_many_locals(false), "overflow_invalid.c0"));
    c0::TypeChecker tc(driver);
    CHECK(!tc.check(*driver.ast()));
    CHECK(driver.has_errors());
  }
}

} // namespace

int main() {
  test_symbol_table();
  test_type_context();
  test_ast_decoration();
  test_bitset_overflow_definite_init();
  std::cout << "All semantic infrastructure tests passed.\n";
  return 0;
}
