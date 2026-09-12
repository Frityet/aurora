#pragma once

#include <cstddef>

namespace aurora {
// Quiesce callbacks and forget any ARAM span borrowed from this MEM2 owner.
void release_aram_mem2_owner(void* memory, std::size_t size);
}
