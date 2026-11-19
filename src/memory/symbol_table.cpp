#include "c0/memory/symbol_table.h"

#include <algorithm>
#include <cstring>

namespace c0 {

namespace {
constexpr uint32_t kInitialBucketCap = 64;
}

SymbolTable::SymbolTable(Arena &arena)
    : arena_(arena), bucket_cap_(kInitialBucketCap) {
  buckets_ = arena_.allocate_array<uint32_t>(bucket_cap_);
  std::fill_n(buckets_, bucket_cap_, kInvalidSymbol);
}

uint64_t SymbolTable::hash_sv(std::string_view sv) noexcept {
  uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : sv) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  return h;
}

uint32_t SymbolTable::find_slot(std::string_view sv) const noexcept {
  const uint32_t mask = bucket_cap_ - 1;
  uint32_t idx = static_cast<uint32_t>(hash_sv(sv)) & mask;
  while (true) {
    const SymbolId entry = buckets_[idx];
    if (entry == kInvalidSymbol || strings_[entry] == sv) {
      return idx;
    }
    idx = (idx + 1) & mask;
  }
}

void SymbolTable::grow_and_rehash() {
  const uint32_t new_cap = bucket_cap_ * 2;
  uint32_t *new_buckets = arena_.allocate_array<uint32_t>(new_cap);
  std::fill_n(new_buckets, new_cap, kInvalidSymbol);

  const uint32_t mask = new_cap - 1;
  for (uint32_t id = 0; id < strings_.size(); ++id) {
    uint32_t idx = static_cast<uint32_t>(hash_sv(strings_[id])) & mask;
    while (new_buckets[idx] != kInvalidSymbol) {
      idx = (idx + 1) & mask;
    }
    new_buckets[idx] = id;
  }

  buckets_ = new_buckets;
  bucket_cap_ = new_cap;
}

SymbolId SymbolTable::intern(std::string_view sv) {
  uint32_t slot = find_slot(sv);
  if (buckets_[slot] != kInvalidSymbol) {
    return buckets_[slot];
  }

  // Keep load factor <= 70% to avoid long linear-probe chains.
  if ((strings_.size() + 1) * 10 >= static_cast<std::size_t>(bucket_cap_) * 7) {
    grow_and_rehash();
    slot = find_slot(sv);
  }

  char *buf = static_cast<char *>(arena_.allocate(sv.size() + 1, 1));
  if (!sv.empty()) {
    std::memcpy(buf, sv.data(), sv.size());
  }
  buf[sv.size()] = '\0';

  const SymbolId id = static_cast<SymbolId>(strings_.size());
  strings_.emplace_back(buf, sv.size());
  buckets_[slot] = id;
  return id;
}

SymbolId SymbolTable::find(std::string_view sv) const noexcept {
  const uint32_t slot = find_slot(sv);
  return buckets_[slot];
}

} // namespace c0
