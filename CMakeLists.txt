cmake_minimum_required(VERSION 3.22.1)

project("SimpleFPEWrapper")

enable_language(CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default -funwind-tables -g -D_THREAD_SAFE -fPIC -stdlib=libc++")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -std=gnu99 -funwind-tables -O3 -fvisibility=hidden")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++")
else()
        # FCL's `fordebug` variant intentionally keeps symbols and Java-side
        # debugging enabled, but AGP otherwise compiles this native library at
        # Clang's default -O0.  The fixed-function wrapper runs on every draw,
        # so an unoptimised C++ build dominates frame time on Android.  Keep
        # debug information while compiling the native hot path as optimised
        # production code.
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fno-semantic-interposition -funwind-tables -g -O3 -fno-math-errno -DNDEBUG -D_THREAD_SAFE -fPIC")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -std=gnu99 -funwind-tables -O3 -fvisibility=hidden")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++")
        if(ANDROID)
                # Only meaningful for CMake's native Android support; AGP/NDK
                # toolchains read ANDROID_STL from Gradle instead.
                set(CMAKE_ANDROID_STL_TYPE c++_static)
        endif()
endif()

# ThinLTO is Clang-specific; GCC only understands full LTO.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto=thin")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -flto=thin")
else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -flto")
endif()

add_library(${CMAKE_PROJECT_NAME} SHARED
        SimpleFPEWrapper/init.cpp
        SimpleFPEWrapper/drawing.cpp
        SimpleFPEWrapper/getter.cpp
        SimpleFPEWrapper/lookup.cpp
        SimpleFPEWrapper/ordered_passthrough.cpp

        SimpleFPEWrapper/backend/loader.cpp

        SimpleFPEWrapper/fpe/fpe.cpp
        SimpleFPEWrapper/fpe/fpe_shadergen.cpp
        SimpleFPEWrapper/fpe/state.cpp
        SimpleFPEWrapper/fpe/transformation.cpp
        SimpleFPEWrapper/fpe/vertexpointer.cpp
        SimpleFPEWrapper/fpe/list.cpp
        SimpleFPEWrapper/fpe/pointer_utils.cpp
        SimpleFPEWrapper/fpe/drawing1x.cpp
        SimpleFPEWrapper/fpe/vertexpointer_utils.cpp
        SimpleFPEWrapper/fpe/drawstate1x.cpp
        SimpleFPEWrapper/fpe/glstate.cpp
        SimpleFPEWrapper/fpe/vertexpointerarray.cpp
)

target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Wextra)

# GCC's LTO value-range analysis reports a false positive inside xxhash's
# byte-copy loop when a 64-byte add() call is inlined (the guarded branch
# is provably dead there: bufferSize + 64 < 32 never holds). The warning
# is emitted at the ltrans stage, out of reach of pragmas and per-TU
# options, so waive these two categories for the LTO link only;
# compile-time diagnostics keep them enabled.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
                -Wno-stringop-overflow -Wno-aggressive-loop-optimizations)
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PUBLIC
        ${CMAKE_SOURCE_DIR}/3rdparty/glm
        ${CMAKE_SOURCE_DIR}/3rdparty/xxhash
)
if(ANDROID)
        target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE log)
endif()

# Host-side tests (not applicable to Android cross builds).
if(NOT ANDROID)
        include(CTest)
        if(BUILD_TESTING)
                add_executable(smoke_dlopen tests/smoke_dlopen.c)
                target_compile_definitions(smoke_dlopen PRIVATE
                        WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                target_link_libraries(smoke_dlopen PRIVATE dl)
                add_dependencies(smoke_dlopen ${CMAKE_PROJECT_NAME})
                add_test(NAME smoke_dlopen COMMAND smoke_dlopen)
        endif()
endif()
