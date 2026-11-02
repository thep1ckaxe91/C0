#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>

namespace c0 {

template <std::size_t DefaultBlockSize = 2 * 1024 * 1024> class ArenaAllocator {
public:
  static constexpr std::size_t kDefaultBlockSize = DefaultBlockSize;

  ArenaAllocator() { allocate_chunk(kDefaultBlockSize); }

  explicit ArenaAllocator(std::size_t initial_capacity) {
    allocate_chunk(std::max(initial_capacity, kDefaultBlockSize));
  }

  ~ArenaAllocator() { reset(); }

  ArenaAllocator(const ArenaAllocator &) = delete;
  ArenaAllocator &operator=(const ArenaAllocator &) = delete;

  ArenaAllocator(ArenaAllocator &&other) noexcept
      : head_chunk_(other.head_chunk_), current_chunk_(other.current_chunk_),
        cleanup_head_(other.cleanup_head_),
        total_allocated_bytes_(other.total_allocated_bytes_),
        chunk_count_(other.chunk_count_) {
    other.head_chunk_ = nullptr;
    other.current_chunk_ = nullptr;
    other.cleanup_head_ = nullptr;
    other.total_allocated_bytes_ = 0;
    other.chunk_count_ = 0;
  }

  ArenaAllocator &operator=(ArenaAllocator &&other) noexcept {
    if (this != &other) {
      reset();
      head_chunk_ = other.head_chunk_;
      current_chunk_ = other.current_chunk_;
      cleanup_head_ = other.cleanup_head_;
      total_allocated_bytes_ = other.total_allocated_bytes_;
      chunk_count_ = other.chunk_count_;
      other.head_chunk_ = nullptr;
      other.current_chunk_ = nullptr;
      other.cleanup_head_ = nullptr;
      other.total_allocated_bytes_ = 0;
      other.chunk_count_ = 0;
    }
    return *this;
  }

  /// Allocates raw uninitialized memory with specified alignment.
  void *allocate(std::size_t size,
                 std::size_t alignment = alignof(std::max_align_t)) {
    if (size == 0) {
      size = 1;
    }

    if (current_chunk_) {
      std::byte *cur = current_chunk_->data + current_chunk_->offset;
      std::uintptr_t cur_addr = reinterpret_cast<std::uintptr_t>(cur);
      std::uintptr_t aligned_addr =
          (cur_addr + (alignment - 1)) & ~(alignment - 1);
      std::size_t padding = aligned_addr - cur_addr;

      if (current_chunk_->offset + padding + size <= current_chunk_->capacity) {
        current_chunk_->offset += padding + size;
        return reinterpret_cast<void *>(aligned_addr);
      }
    }

    // Out-of-memory in current chunk: allocate another chunk of at least
    // kDefaultBlockSize
    std::size_t next_capacity =
        std::max(kDefaultBlockSize, size + alignment + sizeof(Chunk));
    allocate_chunk(next_capacity);

    std::byte *cur = current_chunk_->data + current_chunk_->offset;
    std::uintptr_t cur_addr = reinterpret_cast<std::uintptr_t>(cur);
    std::uintptr_t aligned_addr =
        (cur_addr + (alignment - 1)) & ~(alignment - 1);
    std::size_t padding = aligned_addr - cur_addr;

    current_chunk_->offset += padding + size;
    return reinterpret_cast<void *>(aligned_addr);
  }

  /// Constructs an object of type T inside the arena with arguments forwarded.
  /// If T has a non-trivial destructor, registers it for automatic cleanup on
  /// reset/destruction.
  template <typename T, typename... Args> T *create(Args &&...args) {
    void *mem = allocate(sizeof(T), alignof(T));
    T *obj = new (mem) T(std::forward<Args>(args)...);

    if constexpr (!std::is_trivially_destructible_v<T>) {
      register_destructor(obj, [](void *ptr) { static_cast<T *>(ptr)->~T(); });
    }
    return obj;
  }

  /// Allocates an array of count uninitialized objects.
  template <typename T> T *allocate_array(std::size_t count) {
    if (count == 0)
      return nullptr;
    void *mem = allocate(sizeof(T) * count, alignof(T));
    return reinterpret_cast<T *>(mem);
  }

  /// Cleans up all registered non-trivial destructors and frees all allocated
  /// chunks.
  void reset() {
    CleanupNode *cleanup = cleanup_head_;
    while (cleanup) {
      cleanup->destructor(cleanup->object);
      cleanup = cleanup->next;
    }
    cleanup_head_ = nullptr;

    Chunk *chunk = head_chunk_;
    while (chunk) {
      Chunk *next = chunk->next;
      std::free(chunk);
      chunk = next;
    }
    head_chunk_ = nullptr;
    current_chunk_ = nullptr;
    total_allocated_bytes_ = 0;
    chunk_count_ = 0;
  }

  std::size_t chunk_count() const noexcept { return chunk_count_; }
  std::size_t total_allocated_bytes() const noexcept {
    return total_allocated_bytes_;
  }

private:
  struct Chunk {
    Chunk *next{nullptr};
    std::size_t capacity{0};
    std::size_t offset{0};
    alignas(std::max_align_t) std::byte data[1];
  };

  struct CleanupNode {
    void (*destructor)(void *);
    void *object;
    CleanupNode *next;
  };

  void allocate_chunk(std::size_t capacity) {
    std::size_t total_size = sizeof(Chunk) + capacity;
    void *raw_mem = std::malloc(total_size);
    if (!raw_mem) {
      throw std::bad_alloc();
    }
    Chunk *new_chunk = reinterpret_cast<Chunk *>(raw_mem);
    new_chunk->next = nullptr;
    new_chunk->capacity = capacity;
    new_chunk->offset = 0;

    if (!head_chunk_) {
      head_chunk_ = new_chunk;
      current_chunk_ = new_chunk;
    } else {
      current_chunk_->next = new_chunk;
      current_chunk_ = new_chunk;
    }

    total_allocated_bytes_ += capacity;
    ++chunk_count_;
  }

  void register_destructor(void *obj, void (*destructor_fn)(void *)) {
    void *mem = allocate(sizeof(CleanupNode), alignof(CleanupNode));
    CleanupNode *node = reinterpret_cast<CleanupNode *>(mem);
    node->destructor = destructor_fn;
    node->object = obj;
    node->next = cleanup_head_;
    cleanup_head_ = node;
  }

  Chunk *head_chunk_{nullptr};
  Chunk *current_chunk_{nullptr};
  CleanupNode *cleanup_head_{nullptr};
  std::size_t total_allocated_bytes_{0};
  std::size_t chunk_count_{0};
};

using Arena = ArenaAllocator<2 * 1024 * 1024>;

} // namespace c0
