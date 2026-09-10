#pragma once

#include <cstddef>

namespace aurora {
// Borrow a caller-owned, aligned native MEM2 arena. The caller keeps it alive
// until every SDK/Game heap built in the arena has been destroyed.
void bind_mem2_arena(void* memory, std::size_t size);
void unbind_mem2_arena(void* memory);
}
