find_package(Threads REQUIRED)
add_library(aurora_os STATIC lib/dolphin/os/OSInit.cpp
        lib/dolphin/os/OSMemory.cpp
        lib/dolphin/os/internal.hpp
        lib/dolphin/os/OSBootInfo.cpp
        lib/dolphin/os/OSTime.cpp
        lib/dolphin/os/OSAlarm.cpp
        lib/dolphin/os/OSReset.cpp
        lib/dolphin/arc.cpp
        lib/dolphin/os/OSExecution.cpp
        lib/dolphin/os/OSMutex.cpp
        lib/dolphin/os/OSMessage.cpp
        lib/dolphin/os/OSCache.cpp
        lib/dolphin/os/OSArena.cpp
        lib/dolphin/os/OSAlloc.cpp
        lib/dolphin/os/OSAddress.cpp
        lib/dolphin/os/OSReport.cpp
        lib/dolphin/AR.cpp
        lib/dolphin/PPCArch.cpp
        lib/nand.cpp
        lib/sysconf.cpp
        lib/dolphin/sc/SCSystem.cpp
        lib/dolphin/sc/SCapi.cpp
        lib/dolphin/sc/SCProductInfo.cpp)
add_library(aurora::os ALIAS aurora_os)
set_target_properties(aurora_os PROPERTIES FOLDER "aurora")

target_include_directories(aurora_os PUBLIC include)
target_link_libraries(aurora_os PRIVATE aurora::base Threads::Threads)
