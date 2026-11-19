#pragma once

#include "c0/arena.h"
#include "c0/memory/symbol_table.h"
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace c0 {

struct SemType;

enum class SemTypeKind : uint8_t {
  Invalid = 0,
  Null,
  Void,
  Int,
  Bool,
  Char,
  String,
  Pointer,
  Array,
  Struct,
  Function
};

/// Arena-allocated struct member metadata. Trivially destructible.
struct StructFieldDesc {
  SymbolId name{kInvalidSymbol};
  const SemType *type{nullptr};
  uint32_t index{0};
};
static_assert(std::is_trivially_destructible_v<StructFieldDesc>);

/// Arena-allocated canonical struct descriptor shared across TypeContext and AST.
struct StructDesc {
  SymbolId name{kInvalidSymbol};
  bool defined{false};
  const StructFieldDesc *fields{nullptr};
  uint32_t field_count{0};

  const StructFieldDesc *find_field(SymbolId field_name) const noexcept {
    for (uint32_t i = 0; i < field_count; ++i) {
      if (fields[i].name == field_name) {
        return &fields[i];
      }
    }
    return nullptr;
  }
};
static_assert(std::is_trivially_destructible_v<StructDesc>);

/// Immutable canonical semantic type allocated inside the compiler Arena.
struct SemType {
  SemTypeKind kind{SemTypeKind::Invalid};
  bool has_anon_fn{false};
  uint32_t param_count{0};

  union {
    const SemType *elem{nullptr};
    const StructDesc *struct_desc;
    const SemType *return_type;
  };

  const SemType *const *params{nullptr};
  SymbolId nominal_name{kInvalidSymbol};
  const SemType *canonical_structural{nullptr};

  bool is_invalid() const noexcept { return kind == SemTypeKind::Invalid; }
  bool is_valid() const noexcept { return kind != SemTypeKind::Invalid; }
  bool is_null() const noexcept { return kind == SemTypeKind::Null; }
  bool is_void() const noexcept { return kind == SemTypeKind::Void; }
  bool is_int() const noexcept { return kind == SemTypeKind::Int; }
  bool is_bool() const noexcept { return kind == SemTypeKind::Bool; }
  bool is_char() const noexcept { return kind == SemTypeKind::Char; }
  bool is_string() const noexcept { return kind == SemTypeKind::String; }
  bool is_pointer() const noexcept { return kind == SemTypeKind::Pointer; }
  bool is_array() const noexcept { return kind == SemTypeKind::Array; }
  bool is_struct() const noexcept { return kind == SemTypeKind::Struct; }
  bool is_function() const noexcept { return kind == SemTypeKind::Function; }
  bool is_large() const noexcept { return kind == SemTypeKind::Struct; }
  bool is_undefined_struct() const noexcept {
    return kind == SemTypeKind::Struct &&
           (!struct_desc || !struct_desc->defined);
  }
};
static_assert(std::is_trivially_destructible_v<SemType>);

/// Flyweight factory and interning context for canonical SemType* descriptors.
class TypeContext {
public:
  explicit TypeContext(Arena &arena);

  TypeContext(const TypeContext &) = delete;
  TypeContext &operator=(const TypeContext &) = delete;
  TypeContext(TypeContext &&) = delete;
  TypeContext &operator=(TypeContext &&) = delete;

  const SemType *invalid_type() const noexcept { return invalid_; }
  const SemType *null_type() const noexcept { return null_; }
  const SemType *void_type() const noexcept { return void_; }
  const SemType *int_type() const noexcept { return int_; }
  const SemType *bool_type() const noexcept { return bool_; }
  const SemType *char_type() const noexcept { return char_; }
  const SemType *string_type() const noexcept { return string_; }

  const SemType *get_pointer(const SemType *elem);
  const SemType *get_array(const SemType *elem);

  StructDesc *get_or_create_struct_desc(SymbolId name);
  const SemType *get_struct(SymbolId name);
  void set_struct_fields(StructDesc *desc,
                         std::span<const StructFieldDesc> fields);

  const SemType *get_function(const SemType *ret,
                              std::span<const SemType *const> params,
                              SymbolId nominal_name = kInvalidSymbol);

  static bool same_type(const SemType *lhs, const SemType *rhs) noexcept;
  static bool compatible(const SemType *target, const SemType *value) noexcept;

private:
  struct TypeTable {
    const SemType **buckets{nullptr};
    uint32_t capacity{0};
    uint32_t count{0};
  };

  const SemType *alloc_primitive(SemTypeKind kind);
  const SemType *intern_unary(TypeTable &table, SemTypeKind kind,
                              const SemType *elem);

  Arena &arena_;
  const SemType *invalid_{nullptr};
  const SemType *null_{nullptr};
  const SemType *void_{nullptr};
  const SemType *int_{nullptr};
  const SemType *bool_{nullptr};
  const SemType *char_{nullptr};
  const SemType *string_{nullptr};

  TypeTable ptr_table_;
  TypeTable arr_table_;
  TypeTable fn_table_;

  std::vector<StructDesc *> struct_descs_;
  std::vector<const SemType *> struct_types_;
};

} // namespace c0
