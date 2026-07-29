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

# LTO is for release/shipping builds; iterative test builds link the
# 18MB+ glslang-included library far faster without it (-DSFPEW_LTO=OFF).
option(SFPEW_LTO "Enable link-time optimization" ON)
if(SFPEW_LTO)
        # ThinLTO is Clang-specific; GCC only understands full LTO.
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto=thin")
                set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -flto=thin")
        else()
                set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto")
                set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -flto")
        endif()
endif()

# GLSL 1.10/1.20 -> ESSL 3.00 translation toolchain (plans/09):
# custom preprocessor + glslang -> SPIR-V -> SPIRV-Cross -> ESSL.
# Both are trimmed builds: no HLSL, no CLI tools, no tests.
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/glslang EXCLUDE_FROM_ALL)

set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/SPIRV-Cross EXCLUDE_FROM_ALL)

add_library(${CMAKE_PROJECT_NAME} SHARED
        SimpleFPEWrapper/init.cpp
        SimpleFPEWrapper/drawing.cpp
        SimpleFPEWrapper/getter.cpp
        SimpleFPEWrapper/fbo.cpp
        SimpleFPEWrapper/lookup.cpp
        SimpleFPEWrapper/ordered_passthrough.cpp

        SimpleFPEWrapper/backend/loader.cpp

        SimpleFPEWrapper/fpe/fpe.cpp
        SimpleFPEWrapper/fpe/fpe_shadergen.cpp
        SimpleFPEWrapper/fpe/state.cpp
        SimpleFPEWrapper/fpe/transformation.cpp
        SimpleFPEWrapper/fpe/vertexpointer.cpp
        SimpleFPEWrapper/fpe/list.cpp
        SimpleFPEWrapper/fpe/attribstack.cpp
        SimpleFPEWrapper/fpe/pixelops.cpp
        SimpleFPEWrapper/fpe/selection.cpp
        SimpleFPEWrapper/fpe/evaluators.cpp
        SimpleFPEWrapper/fpe/evaluators.cpp
        SimpleFPEWrapper/shader/translator.cpp
        SimpleFPEWrapper/shader/shaderobjects.cpp
        SimpleFPEWrapper/shader/userprogram.cpp
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
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE
        ${CMAKE_SOURCE_DIR}/3rdparty/glslang
        ${CMAKE_SOURCE_DIR}/3rdparty/SPIRV-Cross
)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
        glslang glslang-default-resource-limits SPIRV spirv-cross-glsl spirv-cross-core)
if(ANDROID)
        target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE log)
endif()

# Host-side tests (not applicable to Android cross builds).
if(NOT ANDROID)
        include(CTest)
        if(BUILD_TESTING)
                foreach(t smoke_dlopen smoke_error smoke_list_recursion smoke_list_replay)
                        add_executable(${t} tests/${t}.c)
                        target_compile_definitions(${t} PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(${t} PRIVATE dl)
                        add_dependencies(${t} ${CMAKE_PROJECT_NAME})
                        add_test(NAME ${t} COMMAND ${t})
                endforeach()

                # Translation pipeline end-to-end test: needs EGL/GLESv2 to
                # also compile the output on a real driver (skips otherwise).
                find_library(SFPEW_TEST_EGL EGL)
                find_library(SFPEW_TEST_GLES GLESv2)
                if(SFPEW_TEST_EGL AND SFPEW_TEST_GLES)
                        add_executable(smoke_list_immediate tests/smoke_list_immediate.c)
                        target_compile_definitions(smoke_list_immediate PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_list_immediate PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_list_immediate ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_list_immediate COMMAND smoke_list_immediate)
                        set_tests_properties(smoke_list_immediate PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_gl3_core tests/smoke_gl3_core.c)
                        target_compile_definitions(smoke_gl3_core PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_gl3_core PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_gl3_core ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_gl3_core COMMAND smoke_gl3_core)
                        set_tests_properties(smoke_gl3_core PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_fog tests/smoke_fog.c)
                        target_compile_definitions(smoke_fog PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_fog PRIVATE dl m ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_fog ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_fog COMMAND smoke_fog)
                        set_tests_properties(smoke_fog PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_mixed_pipeline tests/smoke_mixed_pipeline.c)
                        target_compile_definitions(smoke_mixed_pipeline PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_mixed_pipeline PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_mixed_pipeline ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_mixed_pipeline COMMAND smoke_mixed_pipeline)
                        set_tests_properties(smoke_mixed_pipeline PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_multidraw_native tests/smoke_multidraw_native.c)
                        target_compile_definitions(smoke_multidraw_native PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_multidraw_native PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_multidraw_native ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_multidraw_native COMMAND smoke_multidraw_native)
                        set_tests_properties(smoke_multidraw_native PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_userobject_quads tests/smoke_userobject_quads.c)
                        target_compile_definitions(smoke_userobject_quads PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_userobject_quads PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_userobject_quads ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_userobject_quads COMMAND smoke_userobject_quads)
                        set_tests_properties(smoke_userobject_quads PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_userprog_multiattrib tests/smoke_userprog_multiattrib.c)
                        target_compile_definitions(smoke_userprog_multiattrib PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_userprog_multiattrib PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_userprog_multiattrib ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_userprog_multiattrib COMMAND smoke_userprog_multiattrib)
                        set_tests_properties(smoke_userprog_multiattrib PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_userobject_blit tests/smoke_userobject_blit.c)
                        target_compile_definitions(smoke_userobject_blit PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_userobject_blit PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_userobject_blit ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_userobject_blit COMMAND smoke_userobject_blit)
                        set_tests_properties(smoke_userobject_blit PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_attrib_retire tests/smoke_attrib_retire.c)
                        target_compile_definitions(smoke_attrib_retire PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_attrib_retire PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_attrib_retire ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_attrib_retire COMMAND smoke_attrib_retire)
                        set_tests_properties(smoke_attrib_retire PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_userprog_enable_cache tests/smoke_userprog_enable_cache.c)
                        target_compile_definitions(smoke_userprog_enable_cache PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_userprog_enable_cache PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_userprog_enable_cache ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_userprog_enable_cache COMMAND smoke_userprog_enable_cache)
                        set_tests_properties(smoke_userprog_enable_cache PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_immediate_merge tests/smoke_immediate_merge.c)
                        target_compile_definitions(smoke_immediate_merge PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_immediate_merge PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_immediate_merge ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_immediate_merge COMMAND smoke_immediate_merge)
                        set_tests_properties(smoke_immediate_merge PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_vbo_surface tests/smoke_vbo_surface.c)
                        target_compile_definitions(smoke_vbo_surface PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_vbo_surface PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_vbo_surface ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_vbo_surface COMMAND smoke_vbo_surface)
                        set_tests_properties(smoke_vbo_surface PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_lwjgl_surface tests/smoke_lwjgl_surface.c)
                        target_compile_definitions(smoke_lwjgl_surface PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_lwjgl_surface PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_lwjgl_surface ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_lwjgl_surface COMMAND smoke_lwjgl_surface)
                        set_tests_properties(smoke_lwjgl_surface PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_translator tests/smoke_translator.c)
                        target_compile_definitions(smoke_translator PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_translator PRIVATE dl
                                ${SFPEW_TEST_EGL} ${SFPEW_TEST_GLES})
                        add_dependencies(smoke_translator ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_translator COMMAND smoke_translator)
                        set_tests_properties(smoke_translator PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_render tests/smoke_render.c)
                        target_compile_definitions(smoke_render PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_render PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_render ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_render COMMAND smoke_render)
                        set_tests_properties(smoke_render PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_list_current_attr tests/smoke_list_current_attr.c)
                        target_compile_definitions(smoke_list_current_attr PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_list_current_attr PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_list_current_attr ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_list_current_attr COMMAND smoke_list_current_attr)
                        set_tests_properties(smoke_list_current_attr PROPERTIES SKIP_RETURN_CODE 77)
                endif()

                # Vendored piglit shader_test subset through the mini-runner
                # plans/11 Q5 microbenchmarks: numbers-only, never fails on
                # timing. The binary always builds; the ctest entry only
                # registers with -DSFPEW_BENCH=ON (run `ctest -L bench -V`)
                # so default suite wall time stays flat. SFPEW_BENCH_SCALE
                # scales iteration counts for slow/software devices.
                if(SFPEW_TEST_EGL)
                        option(SFPEW_BENCH "Register bench.fpe as a ctest" OFF)
                        add_executable(sfpew_bench tests/bench_fpe.c)
                        target_compile_definitions(sfpew_bench PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(sfpew_bench PRIVATE dl m ${SFPEW_TEST_EGL})
                        add_dependencies(sfpew_bench ${CMAKE_PROJECT_NAME})
                        if(SFPEW_BENCH)
                                add_test(NAME bench.fpe COMMAND sfpew_bench)
                                set_tests_properties(bench.fpe PROPERTIES
                                        SKIP_RETURN_CODE 77 LABELS bench)
                        endif()
                endif()

                # (tests/piglit/README.md documents provenance and license).
                if(SFPEW_TEST_EGL AND SFPEW_TEST_GLES)
                        add_executable(piglit_runner tests/piglit_runner.c)
                        target_compile_definitions(piglit_runner PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(piglit_runner PRIVATE dl m ${SFPEW_TEST_EGL})
                        add_dependencies(piglit_runner ${CMAKE_PROJECT_NAME})
                        file(GLOB SFPEW_PIGLIT_TESTS ${CMAKE_SOURCE_DIR}/tests/piglit/*.shader_test)
                        file(STRINGS ${CMAKE_SOURCE_DIR}/tests/piglit/KNOWN_DEVIATIONS.txt
                             SFPEW_PIGLIT_XFAIL REGEX "^[^#]")
                        foreach(pt ${SFPEW_PIGLIT_TESTS})
                                get_filename_component(pt_name ${pt} NAME)
                                string(REPLACE ".shader_test" "" pt_name ${pt_name})
                                add_test(NAME piglit.${pt_name} COMMAND piglit_runner ${pt})
                                set_tests_properties(piglit.${pt_name} PROPERTIES SKIP_RETURN_CODE 77)
                                if(pt_name IN_LIST SFPEW_PIGLIT_XFAIL)
                                        # Documented pipeline deviation; if it starts
                                        # passing, WILL_FAIL flags it for delisting.
                                        set_tests_properties(piglit.${pt_name} PROPERTIES WILL_FAIL TRUE)
                                endif()
                        endforeach()
                endif()

                # The committed API manifest must match what lookup.cpp
                # actually resolves (plans/03, 3.5).
                find_package(Python3 COMPONENTS Interpreter)
                if(Python3_FOUND)
                        add_test(NAME api_manifest_current
                                COMMAND ${CMAKE_COMMAND}
                                        -DPY=${Python3_EXECUTABLE}
                                        -DSRC=${CMAKE_SOURCE_DIR}
                                        -DBIN=${CMAKE_BINARY_DIR}
                                        -P ${CMAKE_SOURCE_DIR}/tests/check_manifest.cmake)

                        # Holds the dual-backend contract in docs/backend-support.md:
                        # fails on a backend call outside the GL 3.2 n ES 3.0
                        # intersection that no null check protects, and on a newly
                        # declared entry point nobody has classified yet.
                        add_test(NAME backend_profile_current
                                COMMAND ${Python3_EXECUTABLE}
                                        ${CMAKE_SOURCE_DIR}/tools/check_backend_profile.py
                                        ${CMAKE_SOURCE_DIR}
                                        --check ${CMAKE_SOURCE_DIR}/docs/backend-profile.json)
                endif()
        endif()
endif()
