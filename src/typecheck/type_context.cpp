#include "c0/typecheck/type_context.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace c0 {

namespace {

constexpr uint32_t kInitialTableCap = 32;

inline uint64_t hash_ptr(const void *p) noexcept {
  uint64_t x = reinterpret_cast<std::uintptr_t>(p);
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return x;
}

inline uint64_t hash_combine(uint64_t seed, uint64_t value) noexcept {
  return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

inline uint64_t hash_function_sig(const SemType *ret,
                                  std::span<const SemType *const> params,
                                  SymbolId nominal_name) noexcept {
  uint64_t h = hash_ptr(ret);
  h = hash_combine(h, nominal_name);
  h = hash_combine(h, params.size());
  for (const SemType *param : params) {
    h = hash_combine(h, hash_ptr(param));
  }
  return h;
}

template <typename KeyHashFn>
void rehash_table(Arena &arena, const SemType **&buckets, uint32_t &capacity,
                  KeyHashFn &&hash_fn) {
  const uint32_t new_cap = capacity * 2;
  const SemType **new_buckets = arena.allocate_array<const SemType *>(new_cap);
  std::fill_n(new_buckets, new_cap, nullptr);

  const uint32_t mask = new_cap - 1;
  for (uint32_t i = 0; i < capacity; ++i) {
    const SemType *entry = buckets[i];
    if (!entry) {
      continue;
    }
    uint32_t idx = static_cast<uint32_t>(hash_fn(entry)) & mask;
    while (new_buckets[idx] != nullptr) {
      idx = (idx + 1) & mask;
    }
    new_buckets[idx] = entry;
  }

  buckets = new_buckets;
  capacity = new_cap;
}

void init_table(Arena &arena, const SemType **&buckets, uint32_t &capacity,
                uint32_t &count) {
  capacity = kInitialTableCap;
  count = 0;
  buckets = arena.allocate_array<const SemType *>(capacity);
  std::fill_n(buckets, capacity, nullptr);
}

} // namespace

TypeContext::TypeContext(Arena &arena) : arena_(arena) {
  invalid_ = alloc_primitive(SemTypeKind::Invalid);
  null_ = alloc_primitive(SemTypeKind::Null);
  void_ = alloc_primitive(SemTypeKind::Void);
  int_ = alloc_primitive(SemTypeKind::Int);
  bool_ = alloc_primitive(SemTypeKind::Bool);
  char_ = alloc_primitive(SemTypeKind::Char);
  string_ = alloc_primitive(SemTypeKind::String);

  init_table(arena_, ptr_table_.buckets, ptr_table_.capacity, ptr_table_.count);
  init_table(arena_, arr_table_.buckets, arr_table_.capacity, arr_table_.count);
  init_table(arena_, fn_table_.buckets, fn_table_.capacity, fn_table_.count);
}

const SemType *TypeContext::alloc_primitive(SemTypeKind kind) {
  SemType *type = arena_.create<SemType>();
  type->kind = kind;
  return type;
}

const SemType *TypeContext::intern_unary(TypeTable &table, SemTypeKind kind,
                                         const SemType *elem) {
  if (!elem || elem->is_invalid()) {
    return invalid_;
  }

  const uint64_t h = hash_ptr(elem);
  uint32_t mask = table.capacity - 1;
  uint32_t idx = static_cast<uint32_t>(h) & mask;
  while (table.buckets[idx] != nullptr) {
    if (table.buckets[idx]->elem == elem) {
      return table.buckets[idx];
    }
    idx = (idx + 1) & mask;
  }

  if ((table.count + 1) * 10 >= static_cast<std::size_t>(table.capacity) * 7) {
    rehash_table(arena_, table.buckets, table.capacity,
                 [](const SemType *t) { return hash_ptr(t->elem); });
    mask = table.capacity - 1;
    idx = static_cast<uint32_t>(h) & mask;
    while (table.buckets[idx] != nullptr) {
      idx = (idx + 1) & mask;
    }
  }

  SemType *created = arena_.create<SemType>();
  created->kind = kind;
  created->elem = elem;
  created->has_anon_fn = elem->has_anon_fn;

  table.buckets[idx] = created;
  ++table.count;
  return created;
}

const SemType *TypeContext::get_pointer(const SemType *elem) {
  return intern_unary(ptr_table_, SemTypeKind::Pointer, elem);
}

const SemType *TypeContext::get_array(const SemType *elem) {
  return intern_unary(arr_table_, SemTypeKind::Array, elem);
}

StructDesc *TypeContext::get_or_create_struct_desc(SymbolId name) {
  if (name == kInvalidSymbol) {
    return nullptr;
  }
  if (name >= struct_descs_.size()) {
    struct_descs_.resize(name + 1, nullptr);
    struct_types_.resize(name + 1, nullptr);
  }
  if (struct_descs_[name] == nullptr) {
    StructDesc *desc = arena_.create<StructDesc>();
    desc->name = name;
    struct_descs_[name] = desc;

    SemType *st = arena_.create<SemType>();
    st->kind = SemTypeKind::Struct;
    st->struct_desc = desc;
    struct_types_[name] = st;
  }
  return struct_descs_[name];
}

const SemType *TypeContext::get_struct(SymbolId name) {
  if (get_or_create_struct_desc(name) == nullptr) {
    return invalid_;
  }
  return struct_types_[name];
}

void TypeContext::set_struct_fields(StructDesc *desc,
                                    std::span<const StructFieldDesc> fields) {
  if (!desc) {
    return;
  }
  if (fields.empty()) {
    desc->fields = nullptr;
    desc->field_count = 0;
  } else {
    StructFieldDesc *copied =
        arena_.allocate_array<StructFieldDesc>(fields.size());
    std::memcpy(copied, fields.data(), fields.size() * sizeof(StructFieldDesc));
    desc->fields = copied;
    desc->field_count = static_cast<uint32_t>(fields.size());
  }
  desc->defined = true;
}

const SemType *TypeContext::get_function(const SemType *ret,
                                         std::span<const SemType *const> params,
                                         SymbolId nominal_name) {
  if (!ret || ret->is_invalid()) {
    return invalid_;
  }
  for (const SemType *param : params) {
    if (!param || param->is_invalid()) {
      return invalid_;
    }
  }

  const SemType *canonical = nullptr;
  if (nominal_name != kInvalidSymbol) {
    canonical = get_function(ret, params, kInvalidSymbol);
  }

  const uint64_t h = hash_function_sig(ret, params, nominal_name);
  uint32_t mask = fn_table_.capacity - 1;
  uint32_t idx = static_cast<uint32_t>(h) & mask;

  auto matches = [&](const SemType *entry) noexcept {
    if (entry->nominal_name != nominal_name || entry->return_type != ret ||
        entry->param_count != params.size()) {
      return false;
    }
    for (uint32_t i = 0; i < entry->param_count; ++i) {
      if (entry->params[i] != params[i]) {
        return false;
      }
    }
    return true;
  };

  while (fn_table_.buckets[idx] != nullptr) {
    if (matches(fn_table_.buckets[idx])) {
      return fn_table_.buckets[idx];
    }
    idx = (idx + 1) & mask;
  }

  if ((fn_table_.count + 1) * 10 >=
      static_cast<std::size_t>(fn_table_.capacity) * 7) {
    rehash_table(arena_, fn_table_.buckets, fn_table_.capacity,
                 [](const SemType *t) {
                   return hash_function_sig(
                       t->return_type,
                       std::span<const SemType *const>(t->params, t->param_count),
                       t->nominal_name);
                 });
    mask = fn_table_.capacity - 1;
    idx = static_cast<uint32_t>(h) & mask;
    while (fn_table_.buckets[idx] != nullptr) {
      idx = (idx + 1) & mask;
    }
  }

  SemType *created = arena_.create<SemType>();
  created->kind = SemTypeKind::Function;
  created->return_type = ret;
  created->param_count = static_cast<uint32_t>(params.size());
  created->nominal_name = nominal_name;

  if (!params.empty()) {
    const SemType **copied_params =
        arena_.allocate_array<const SemType *>(params.size());
    std::memcpy(copied_params, params.data(),
                params.size() * sizeof(const SemType *));
    created->params = copied_params;
  }

  created->canonical_structural =
      (nominal_name == kInvalidSymbol) ? created : canonical;

  bool anon = (nominal_name == kInvalidSymbol) || ret->has_anon_fn;
  if (!anon) {
    for (const SemType *p : params) {
      if (p->has_anon_fn) {
        anon = true;
        break;
      }
    }
  }
  created->has_anon_fn = anon;

  fn_table_.buckets[idx] = created;
  ++fn_table_.count;
  return created;
}

bool TypeContext::same_type(const SemType *lhs, const SemType *rhs) noexcept {
  if (!lhs || !rhs || lhs->is_invalid() || rhs->is_invalid() ||
      lhs->is_null() || rhs->is_null()) {
    return false;
  }
  if (lhs == rhs) {
    return true;
  }
  if (!lhs->has_anon_fn && !rhs->has_anon_fn) {
    return false;
  }
  if (lhs->kind != rhs->kind) {
    return false;
  }

  switch (lhs->kind) {
  case SemTypeKind::Pointer:
  case SemTypeKind::Array:
    return same_type(lhs->elem, rhs->elem);
  case SemTypeKind::Function:
    if (lhs->nominal_name != kInvalidSymbol &&
        rhs->nominal_name != kInvalidSymbol &&
        lhs->nominal_name != rhs->nominal_name) {
      return false;
    }
    if (lhs->canonical_structural == rhs->canonical_structural) {
      return true;
    }
    if (lhs->param_count != rhs->param_count ||
        !same_type(lhs->return_type, rhs->return_type)) {
      return false;
    }
    for (uint32_t i = 0; i < lhs->param_count; ++i) {
      if (!same_type(lhs->params[i], rhs->params[i])) {
        return false;
      }
    }
    return true;
  default:
    return false;
  }
}

bool TypeContext::compatible(const SemType *target,
                             const SemType *value) noexcept {
  if (!target || !value || target->is_invalid() || value->is_invalid()) {
    return true;
  }
  if (value->is_null()) {
    return target->is_pointer();
  }
  return same_type(target, value);
}

} // namespace c0
