add_library(aurora_nw4r STATIC
  lib/nw4r/brlan.cpp
)
add_library(aurora::nw4r ALIAS aurora_nw4r)
set_target_properties(aurora_nw4r PROPERTIES FOLDER "aurora")

target_include_directories(aurora_nw4r PUBLIC include)

# Original NW4R evaluates the curve polynomial without fused multiply/add operations.
target_compile_options(aurora_nw4r PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffp-contract=off>
  $<$<CXX_COMPILER_ID:MSVC>:/fp:strict>
)
