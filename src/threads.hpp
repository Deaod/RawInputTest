#pragma once
#include "types.hpp"

namespace threads {
namespace current {

void assign_id();
void release_id();
u32 id();

} // namespace current

u32 max_assigned_id();

} // namespace threads