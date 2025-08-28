
get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(isMultiConfig)
    if(NOT "Msan" IN_LIST CMAKE_CONFIGURATION_TYPES)
        list(APPEND CMAKE_CONFIGURATION_TYPES Msan)
    endif()
endif()

# Note you must use clang for Memory Sanitizer to be supported.
# Build using flags -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang

set(CMAKE_C_FLAGS_MSAN
        "${CMAKE_C_FLAGS_DEBUG} -fsanitize=memory -fsanitize-memory-track-origins -fno-omit-frame-pointer -fPIE" CACHE STRING
        "Flags used by the C compiler for Msan build type or configuration." FORCE)

set(CMAKE_CXX_FLAGS_MSAN
        "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=memory -fsanitize-memory-track-origins -fno-omit-frame-pointer -fPIE" CACHE STRING
        "Flags used by the C++ compiler for Msan build type or configuration." FORCE)

set(CMAKE_EXE_LINKER_FLAGS_MSAN
        "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=memory -fsanitize-memory-track-origins -fPIE" CACHE STRING
        "Linker flags to be used to create executables for Msan build type." FORCE)

set(CMAKE_SHARED_LINKER_FLAGS_MSAN
        "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -fsanitize=memory -fsanitize-memory-track-origins -fPIE" CACHE STRING
        "Linker lags to be used to create shared libraries for Msan build type." FORCE)
