

#include <cstddef>
#include <memory>

template<std::size_t block_size>
class ArenaAllocator {
public:
  using Marker = std::size_t;

  ArenaAllocator(const ArenaAllocator &) = delete;
  ArenaAllocator &operator=(const ArenaAllocator &) = delete;

  explicit ArenaAllocator(std::size_t cap);

  template<typename T, typename ...Args>
  T *allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));
  Marker get_marker() const;
  void free_to_marker(Marker marker);
  void reset();

private:
  std::unique_ptr<std::byte[]> m_memory;
  std::size_t m_capacity;
  std::size_t m_offset;
};