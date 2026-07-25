cmake_minimum_required(VERSION 3.22.1)

project("SimpleFPEWrapper")

enable_language(CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MACOS)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w -fvisibility=default -funwind-tables -g -D_THREAD_SAFE -fPIC -stdlib=libc++")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w -g -std=gnu99 -funwind-tables -O3 -fvisibility=hidden")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++")
else()
        # FCL's `fordebug` variant intentionally keeps symbols and Java-side
        # debugging enabled, but AGP otherwise compiles this native library at
        # Clang's default -O0.  The fixed-function wrapper runs on every draw,
        # so an unoptimised C++ build dominates frame time on Android.  Keep
        # debug information while compiling the native hot path as optimised
        # production code.
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w -fvisibility=hidden -fno-semantic-interposition -funwind-tables -g -O3 -ffast-math -flto=thin -DNDEBUG -D_THREAD_SAFE -fPIC")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w -g -std=gnu99 -funwind-tables -O3 -fvisibility=hidden")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -flto=thin")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++")
        set(CMAKE_ANDROID_STL_TYPE c++_static)
endif()

add_library(${CMAKE_PROJECT_NAME} SHARED
        SimpleFPEWrapper/init.cpp
        SimpleFPEWrapper/drawing.cpp
        SimpleFPEWrapper/getter.cpp
        SimpleFPEWrapper/lookup.cpp

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

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/3rdparty/glm
        ${CMAKE_SOURCE_DIR}/3rdparty/xxhash
)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE )
