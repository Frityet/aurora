add_library(aurora_msl STATIC lib/MSL_C/printf.cpp)
add_library(aurora::msl ALIAS aurora_msl)
set_target_properties(aurora_msl PROPERTIES FOLDER "aurora")

target_include_directories(aurora_msl PUBLIC include)

# Do not force-include MSL_C/stdio.h here: numeric conversions in the provider
# deliberately call the host C runtime instead of its own redirected exports.
