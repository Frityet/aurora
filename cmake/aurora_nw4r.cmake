add_library(aurora_nw4r STATIC
  lib/nw4r/brlan.cpp
)
add_library(aurora::nw4r ALIAS aurora_nw4r)
set_target_properties(aurora_nw4r PROPERTIES FOLDER "aurora")

target_include_directories(aurora_nw4r PUBLIC include)
