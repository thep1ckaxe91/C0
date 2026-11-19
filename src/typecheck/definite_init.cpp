#include "c0/typecheck/definite_init.h"

#include "c0/ast.h"
#include "c0/driver.h"
#include <algorithm>
#include <utility>

namespace c0 {

namespace {

uint64_t mask_for_bits(uint32_t count) noexcept {
  if (count == 0) {
    return 0;
  }
  if (count >= 64) {
    return ~0ULL;
  }
  return (1ULL << count) - 1ULL;
}

} // namespace

InitBitset::InitBitset(uint32_t num_bits) : num_bits_(num_bits) {
  if (num_bits > 64) {
    const uint32_t extra_words = ((num_bits - 64) + 63) / 64;
    overflow_.assign(extra_words, 0);
  }
}

InitBitset InitBitset::all_ones(uint32_t num_bits) {
  InitBitset bs(num_bits);
  bs.inline_bits_ = mask_for_bits(num_bits);
  if (!bs.overflow_.empty()) {
    uint32_t remaining = num_bits - 64;
    for (std::size_t i = 0; i < bs.overflow_.size(); ++i) {
      bs.overflow_[i] = mask_for_bits(remaining);
      remaining = (remaining > 64) ? (remaining - 64) : 0;
    }
  }
  return bs;
}

void InitBitset::set(uint32_t bit) {
  if (bit >= num_bits_) {
    return;
  }
  if (bit < 64) {
    inline_bits_ |= (1ULL << bit);
  } else {
    overflow_[(bit >> 6) - 1] |= (1ULL << (bit & 63U));
  }
}

void InitBitset::clear(uint32_t bit) {
  if (bit >= num_bits_) {
    return;
  }
  if (bit < 64) {
    inline_bits_ &= ~(1ULL << bit);
  } else {
    overflow_[(bit >> 6) - 1] &= ~(1ULL << (bit & 63U));
  }
}

bool InitBitset::test(uint32_t bit) const noexcept {
  if (bit >= num_bits_) {
    return true;
  }
  if (bit < 64) {
    return (inline_bits_ & (1ULL << bit)) != 0;
  }
  return (overflow_[(bit >> 6) - 1] & (1ULL << (bit & 63U))) != 0;
}

void InitBitset::intersect_with(const InitBitset &other) noexcept {
  inline_bits_ &= other.inline_bits_;
  const std::size_t words = std::min(overflow_.size(), other.overflow_.size());
  for (std::size_t i = 0; i < words; ++i) {
    overflow_[i] &= other.overflow_[i];
  }
}

namespace {

struct InitState {
  InitBitset must_init;
  bool reachable{true};
};

struct LoopContinueState {
  InitBitset continue_init;
  bool has_continue{false};
};

class DefiniteInitRunner {
public:
  explicit DefiniteInitRunner(Driver &driver) : driver_(driver) {}

  void run(const Program &program) {
    for (const TopDecl *decl : program.decls) {
      if (decl && decl->ast_kind == ASTKind::FnDef) {
        const auto *fn = static_cast<const FnDef *>(decl);
        check_callable(fn->params, fn->body, fn->local_count);
      }
    }
    for (const LambdaExp *lambda : program.hoisted_lambdas) {
      if (lambda && lambda->body) {
        check_callable(lambda->params, lambda->body, lambda->local_count);
      }
    }
  }

private:
  void check_callable(const std::vector<ParamDecl *> &params,
                      const BlockStmt *body, uint32_t local_count) {
    num_slots_ = local_count;
    loop_stack_.clear();

    InitState state{InitBitset(num_slots_), true};
    for (const ParamDecl *param : params) {
      if (param && param->slot_id < num_slots_) {
        state.must_init.set(param->slot_id);
      }
    }
    if (body) {
      check_block(*body, state);
    }
  }

  void check_block(const BlockStmt &block, InitState &state) {
    for (const Stmt *stmt : block.stmts) {
      if (!stmt) {
        continue;
      }
      check_stmt(*stmt, state);
    }
  }

  void check_stmt(const Stmt &stmt, InitState &state) {
    if (!state.reachable) {
      return;
    }

    switch (stmt.ast_kind) {
    case ASTKind::BlockStmt:
      check_block(static_cast<const BlockStmt &>(stmt), state);
      break;

    case ASTKind::VarDeclStmt: {
      const auto &decl = static_cast<const VarDeclStmt &>(stmt);
      if (decl.slot_id < num_slots_) {
        state.must_init.clear(decl.slot_id);
      }
      if (decl.init) {
        check_expr(*decl.init, state);
        if (decl.slot_id < num_slots_) {
          state.must_init.set(decl.slot_id);
        }
      }
      break;
    }

    case ASTKind::AssignStmt: {
      const auto &assign = static_cast<const AssignStmt &>(stmt);
      if (assign.op == AssignOp::ASSIGN && assign.lhs &&
          assign.lhs->ast_kind == ASTKind::VarExp) {
        const auto *var = static_cast<const VarExp *>(assign.lhs);
        if (assign.rhs) {
          check_expr(*assign.rhs, state);
        }
        if ((var->symbol_kind == SymbolKind::LocalVar ||
             var->symbol_kind == SymbolKind::Param) &&
            var->slot_id < num_slots_) {
          state.must_init.set(var->slot_id);
        }
      } else {
        if (assign.lhs) {
          check_expr(*assign.lhs, state);
        }
        if (assign.rhs) {
          check_expr(*assign.rhs, state);
        }
      }
      break;
    }

    case ASTKind::IncDecStmt: {
      const auto &inc = static_cast<const IncDecStmt &>(stmt);
      if (inc.target) {
        check_expr(*inc.target, state);
      }
      break;
    }

    case ASTKind::ExprStmt: {
      const auto &es = static_cast<const ExprStmt &>(stmt);
      if (es.expr) {
        check_expr(*es.expr, state);
      }
      break;
    }

    case ASTKind::IfStmt: {
      const auto &ifs = static_cast<const IfStmt &>(stmt);
      if (ifs.cond) {
        check_expr(*ifs.cond, state);
      }
      InitState then_state = state;
      if (ifs.then_branch) {
        check_stmt(*ifs.then_branch, then_state);
      }
      InitState else_state = state;
      if (ifs.else_branch) {
        check_stmt(*ifs.else_branch, else_state);
      }

      if (then_state.reachable && else_state.reachable) {
        then_state.must_init.intersect_with(else_state.must_init);
        state = std::move(then_state);
      } else if (then_state.reachable) {
        state = std::move(then_state);
      } else if (else_state.reachable) {
        state = std::move(else_state);
      } else {
        state.reachable = false;
      }
      break;
    }

    case ASTKind::WhileStmt: {
      const auto &loop = static_cast<const WhileStmt &>(stmt);
      if (loop.cond) {
        check_expr(*loop.cond, state);
      }
      InitState post_cond = state;
      loop_stack_.push_back({InitBitset::all_ones(num_slots_), false});
      InitState body_state = post_cond;
      if (loop.body) {
        check_stmt(*loop.body, body_state);
      }
      loop_stack_.pop_back();
      state = std::move(post_cond);
      break;
    }

    case ASTKind::ForStmt: {
      const auto &loop = static_cast<const ForStmt &>(stmt);
      if (loop.init) {
        check_stmt(*loop.init, state);
      }
      if (loop.cond) {
        check_expr(*loop.cond, state);
      }
      InitState post_cond = state;
      loop_stack_.push_back({InitBitset::all_ones(num_slots_), false});
      InitState body_state = post_cond;
      if (loop.body) {
        check_stmt(*loop.body, body_state);
      }
      LoopContinueState cont = std::move(loop_stack_.back());
      loop_stack_.pop_back();

      if (loop.step) {
        InitState step_state{InitBitset(num_slots_), false};
        if (body_state.reachable && cont.has_continue) {
          body_state.must_init.intersect_with(cont.continue_init);
          step_state = std::move(body_state);
        } else if (body_state.reachable) {
          step_state = std::move(body_state);
        } else if (cont.has_continue) {
          step_state.must_init = std::move(cont.continue_init);
          step_state.reachable = true;
        }
        if (step_state.reachable) {
          check_stmt(*loop.step, step_state);
        }
      }
      state = std::move(post_cond);
      break;
    }

    case ASTKind::ReturnStmt: {
      const auto &ret = static_cast<const ReturnStmt &>(stmt);
      if (ret.val) {
        check_expr(*ret.val, state);
      }
      state.reachable = false;
      break;
    }

    case ASTKind::ErrorStmt: {
      const auto &err = static_cast<const ErrorStmt &>(stmt);
      if (err.msg) {
        check_expr(*err.msg, state);
      }
      state.reachable = false;
      break;
    }

    case ASTKind::AssertStmt: {
      const auto &asrt = static_cast<const AssertStmt &>(stmt);
      if (asrt.cond) {
        check_expr(*asrt.cond, state);
      }
      break;
    }

    case ASTKind::BreakStmt:
      state.reachable = false;
      break;

    case ASTKind::ContinueStmt:
      if (!loop_stack_.empty()) {
        auto &top = loop_stack_.back();
        if (!top.has_continue) {
          top.continue_init = state.must_init;
          top.has_continue = true;
        } else {
          top.continue_init.intersect_with(state.must_init);
        }
      }
      state.reachable = false;
      break;

    default:
      break;
    }
  }

  void check_expr(const Exp &expr, const InitState &state) {
    switch (expr.ast_kind) {
    case ASTKind::VarExp: {
      const auto &var = static_cast<const VarExp &>(expr);
      if ((var.symbol_kind == SymbolKind::LocalVar ||
           var.symbol_kind == SymbolKind::Param) &&
          var.slot_id < num_slots_ && !state.must_init.test(var.slot_id)) {
        driver_.report_error(var.range.start,
                             "variable '" + var.name +
                                 "' may be used uninitialized");
      }
      break;
    }

    case ASTKind::BinaryExp: {
      const auto &bin = static_cast<const BinaryExp &>(expr);
      if (bin.lhs) {
        check_expr(*bin.lhs, state);
      }
      if (bin.rhs) {
        check_expr(*bin.rhs, state);
      }
      break;
    }

    case ASTKind::UnaryExp: {
      const auto &un = static_cast<const UnaryExp &>(expr);
      if (un.op != UnaryOp::ADDR_OF && un.operand) {
        check_expr(*un.operand, state);
      }
      break;
    }

    case ASTKind::TernaryExp: {
      const auto &tern = static_cast<const TernaryExp &>(expr);
      if (tern.cond) {
        check_expr(*tern.cond, state);
      }
      if (tern.true_branch) {
        check_expr(*tern.true_branch, state);
      }
      if (tern.false_branch) {
        check_expr(*tern.false_branch, state);
      }
      break;
    }

    case ASTKind::CallExp: {
      const auto &call = static_cast<const CallExp &>(expr);
      if (call.callee) {
        check_expr(*call.callee, state);
      }
      for (const Exp *arg : call.args) {
        if (arg) {
          check_expr(*arg, state);
        }
      }
      break;
    }

    case ASTKind::FieldExp: {
      const auto &field = static_cast<const FieldExp &>(expr);
      if (field.base) {
        check_expr(*field.base, state);
      }
      break;
    }

    case ASTKind::IndexExp: {
      const auto &idx = static_cast<const IndexExp &>(expr);
      if (idx.base) {
        check_expr(*idx.base, state);
      }
      if (idx.index) {
        check_expr(*idx.index, state);
      }
      break;
    }

    case ASTKind::AllocArrayExp: {
      const auto &alloc = static_cast<const AllocArrayExp &>(expr);
      if (alloc.count) {
        check_expr(*alloc.count, state);
      }
      break;
    }

    case ASTKind::LambdaExp:
    case ASTKind::IntLitExp:
    case ASTKind::BoolLitExp:
    case ASTKind::CharLitExp:
    case ASTKind::StringLitExp:
    case ASTKind::NullLitExp:
    case ASTKind::AllocExp:
    default:
      break;
    }
  }

  Driver &driver_;
  uint32_t num_slots_{0};
  std::vector<LoopContinueState> loop_stack_;
};

} // namespace

bool DefiniteInitChecker::check(const Program &program, Driver &driver) {
  const std::size_t initial_errors = driver.error_count();
  DefiniteInitRunner runner(driver);
  runner.run(program);
  return driver.error_count() == initial_errors;
}

} // namespace c0
