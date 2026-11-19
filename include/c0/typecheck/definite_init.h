#pragma once

#include <cstdint>
#include <vector>

namespace c0 {

class Driver;
class Program;

/// Bitset tracking definitely-initialized local variable slots.
/// Uses inline 64-bit storage for functions with <= 64 locals (zero heap allocation).
class InitBitset {
public:
  InitBitset() = default;
  explicit InitBitset(uint32_t num_bits);

  static InitBitset all_ones(uint32_t num_bits);

  void set(uint32_t bit);
  void clear(uint32_t bit);
  bool test(uint32_t bit) const noexcept;
  void intersect_with(const InitBitset &other) noexcept;

  uint32_t size() const noexcept { return num_bits_; }

private:
  uint32_t num_bits_{0};
  uint64_t inline_bits_{0};
  std::vector<uint64_t> overflow_;
};

/// Forward must-initialization dataflow pass over AST functions and hoisted lambdas.
class DefiniteInitChecker {
public:
  static bool check(const Program &program, Driver &driver);
};

} // namespace c0
