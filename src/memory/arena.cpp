#include "c0/arena.h"

namespace c0 {

// Explicit template instantiation for 2MB Arena
template class ArenaAllocator<2 * 1024 * 1024>;

} // namespace c0
