#pragma once

#include "c0/arena.h"
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace c0 {

using SymbolId = uint32_t;
inline constexpr SymbolId kInvalidSymbol = std::numeric_limits<uint32_t>::max();

/// Contiguous, arena-backed identifier interning pool mapping strings to dense
/// uint32_t handles via an open-addressing hash table.
class SymbolTable {
public:
  explicit SymbolTable(Arena &arena);

  SymbolTable(const SymbolTable &) = delete;
  SymbolTable &operator=(const SymbolTable &) = delete;
  SymbolTable(SymbolTable &&) = delete;
  SymbolTable &operator=(SymbolTable &&) = delete;

  /// Interns `sv` into the arena and returns its dense canonical SymbolId.
  SymbolId intern(std::string_view sv);

  /// Pure lookup returning kInvalidSymbol if `sv` has not been interned.
  SymbolId find(std::string_view sv) const noexcept;

  /// Resolves a SymbolId back to its null-terminated arena string_view.
  std::string_view resolve(SymbolId id) const noexcept {
    return (id < strings_.size()) ? strings_[id] : std::string_view{};
  }

  std::size_t size() const noexcept { return strings_.size(); }

private:
  static uint64_t hash_sv(std::string_view sv) noexcept;
  uint32_t find_slot(std::string_view sv) const noexcept;
  void grow_and_rehash();

  Arena &arena_;
  std::vector<std::string_view> strings_;
  uint32_t *buckets_{nullptr};
  uint32_t bucket_cap_{0};
};

} // namespace c0
