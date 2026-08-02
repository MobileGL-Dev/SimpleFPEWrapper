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
        # so an unoptimized C++ build dominates frame time on Android.  Keep
        # debug information while compiling the native hot path as optimized
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
        # These flags reach every target, so glslang's and SPIRV-Cross's
        # static libraries are archives of LTO bitcode - which plain `ar`
        # can only index through a bfd plugin whose LLVM/GCC version must
        # match the compiler's. Ubuntu ships that plugin under a versioned
        # name (LLVMgold-18.so) and no unversioned llvm-ar, so CMake falls
        # back to /usr/bin/ar and a mismatched or missing plugin yields an
        # archive with an EMPTY symbol index: the linker pulls in no member,
        # a shared library links anyway (undefined symbols are legal there),
        # and the .so only fails at dlopen. CMAKE_<LANG>_COMPILER_AR is the
        # archiver shipped with this exact compiler - version-matched by
        # construction and needing no plugin at all.
        if(CMAKE_CXX_COMPILER_AR AND CMAKE_CXX_COMPILER_RANLIB)
                set(CMAKE_AR "${CMAKE_CXX_COMPILER_AR}")
                set(CMAKE_RANLIB "${CMAKE_CXX_COMPILER_RANLIB}")
        else()
                # No archiver that is certain to handle this compiler's
                # bitcode. Losing LTO is a slower library; guessing wrong
                # is a library that only fails at dlopen.
                message(WARNING "SFPEW: no LTO-capable archiver for ${CMAKE_CXX_COMPILER_ID} "
                                "(CMAKE_CXX_COMPILER_AR unset) - building without LTO.")
                set(SFPEW_LTO OFF)
                string(REPLACE " -flto=thin" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
                string(REPLACE " -flto" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
                string(REPLACE " -flto=thin" "" CMAKE_SHARED_LINKER_FLAGS
                       "${CMAKE_SHARED_LINKER_FLAGS}")
                string(REPLACE " -flto" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
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
# glslang defaults ENABLE_RTTI off, which makes it compile ITSELF with
# -fno-rtti, so it never emits `typeinfo for glslang::TProgram`. This
# library needs RTTI (dynamic_cast in the display-list and matrix paths) and
# constructs a glslang::TProgram, a polymorphic type - so our RTTI-enabled
# translation unit references a typeinfo the -fno-rtti glslang never
# defines, and the .so fails to dlopen with an undefined symbol.
#
# It only shows up in some configurations, which is why it went unnoticed:
# full LTO (the plain gcc job) resolves the reference away, while ThinLTO
# and the sanitizer build do not - UBSan's vptr check needs RTTI for every
# polymorphic type by construction. Match glslang's ABI to ours instead of
# relying on an optimizer to hide the mismatch.
set(ENABLE_RTTI ON CACHE BOOL "" FORCE)
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

# Display-list geometry accounting and the switches that bisect the batched
# replay. Investigation scaffolding: out of a normal build entirely, and even
# when compiled in it stays dormant until SFPEW_LISTLOG is set at runtime.
option(SFPEW_LIST_DEBUG "Compile the display-list accounting and its bisection switches" OFF)
if(SFPEW_LIST_DEBUG)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SFPEW_LIST_DEBUG=1)
endif()

# The git commit this build came from, logged once at backend initialization
# so a device report always identifies the code it was produced by - version
# strings alone cannot distinguish two builds made the same day. Configure
# time is good enough: the hash marks the checkout, and "-dirty" marks
# uncommitted changes on top of it. Falls back to "unknown" outside a git
# checkout (tarball builds).
find_package(Git QUIET)
set(SFPEW_GIT_COMMIT "unknown")
if(GIT_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_SOURCE_DIR} rev-parse --short=12 HEAD
                        OUTPUT_VARIABLE SFPEW_GIT_COMMIT_RAW
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET RESULT_VARIABLE SFPEW_GIT_RC)
        if(SFPEW_GIT_RC EQUAL 0)
                execute_process(COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_SOURCE_DIR} diff --quiet HEAD
                                RESULT_VARIABLE SFPEW_GIT_DIRTY ERROR_QUIET)
                set(SFPEW_GIT_COMMIT "${SFPEW_GIT_COMMIT_RAW}")
                if(NOT SFPEW_GIT_DIRTY EQUAL 0)
                        set(SFPEW_GIT_COMMIT "${SFPEW_GIT_COMMIT}-dirty")
                endif()
        endif()
endif()
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SFPEW_GIT_COMMIT="${SFPEW_GIT_COMMIT}")

# A shared library may legally carry undefined symbols, so a link that
# quietly dropped a static-library member (a bitcode archive the archiver
# could not index, a missing typeinfo) still produced a .so - and the
# failure surfaced only as a dlopen error in every single test. The
# library resolves the host GL at runtime through dlsym, never through
# the dynamic linker, so it genuinely has nothing left to leave
# undefined: demand that here and fail at build time instead.
if(NOT APPLE)
        target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,--no-undefined)
endif()

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

                # tools/glbench: standalone CPU-overhead benchmark for ANY
                # library exporting GL 1.x (this wrapper, gl4es, a system
                # libGL). Not a ctest - it takes a library path as input, and
                # comparing implementations means running it more than once.
                # Built when EGL is present so it is always available next to
                # the library it measures; drive it with
                # tools/glbench-compare.sh.
                find_library(SFPEW_GLBENCH_EGL EGL)
                if(SFPEW_GLBENCH_EGL)
                        add_executable(glbench tools/glbench.c)
                        target_link_libraries(glbench PRIVATE dl ${SFPEW_GLBENCH_EGL})
                endif()

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

                        add_executable(smoke_buffer_name_reuse tests/smoke_buffer_name_reuse.c)
                        target_compile_definitions(smoke_buffer_name_reuse PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_buffer_name_reuse PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_buffer_name_reuse ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_buffer_name_reuse COMMAND smoke_buffer_name_reuse)
                        set_tests_properties(smoke_buffer_name_reuse PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_swap_flushes_batch tests/smoke_swap_flushes_batch.c)
                        target_compile_definitions(smoke_swap_flushes_batch PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_swap_flushes_batch PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_swap_flushes_batch ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_swap_flushes_batch COMMAND smoke_swap_flushes_batch)
                        set_tests_properties(smoke_swap_flushes_batch PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_dlist_no_state_leak tests/smoke_dlist_no_state_leak.c)
                        target_compile_definitions(smoke_dlist_no_state_leak PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_dlist_no_state_leak PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_dlist_no_state_leak ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_dlist_no_state_leak COMMAND smoke_dlist_no_state_leak)
                        set_tests_properties(smoke_dlist_no_state_leak PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_beginend_coverage tests/smoke_beginend_coverage.c)
                        target_compile_definitions(smoke_beginend_coverage PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_beginend_coverage PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_beginend_coverage ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_beginend_coverage COMMAND smoke_beginend_coverage)
                        set_tests_properties(smoke_beginend_coverage PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_empty_beginend tests/smoke_empty_beginend.c)
                        target_compile_definitions(smoke_empty_beginend PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_empty_beginend PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_empty_beginend ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_empty_beginend COMMAND smoke_empty_beginend)
                        set_tests_properties(smoke_empty_beginend PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_degenerate_prims tests/smoke_degenerate_prims.c)
                        target_compile_definitions(smoke_degenerate_prims PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_degenerate_prims PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_degenerate_prims ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_degenerate_prims COMMAND smoke_degenerate_prims)
                        set_tests_properties(smoke_degenerate_prims PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_polygon_mode tests/smoke_polygon_mode.c)
                        target_compile_definitions(smoke_polygon_mode PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_polygon_mode PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_polygon_mode ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_polygon_mode COMMAND smoke_polygon_mode)
                        set_tests_properties(smoke_polygon_mode PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_edgeflag tests/smoke_edgeflag.c)
                        target_compile_definitions(smoke_edgeflag PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_edgeflag PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_edgeflag ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_edgeflag COMMAND smoke_edgeflag)
                        set_tests_properties(smoke_edgeflag PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_dlist_beginend tests/smoke_dlist_beginend.c)
                        target_compile_definitions(smoke_dlist_beginend PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_dlist_beginend PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_dlist_beginend ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_dlist_beginend COMMAND smoke_dlist_beginend)
                        set_tests_properties(smoke_dlist_beginend PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_rastercolor tests/smoke_rastercolor.c)
                        target_compile_definitions(smoke_rastercolor PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_rastercolor PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_rastercolor ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_rastercolor COMMAND smoke_rastercolor)
                        set_tests_properties(smoke_rastercolor PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_line_loop tests/smoke_line_loop.c)
                        target_compile_definitions(smoke_line_loop PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_line_loop PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_line_loop ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_line_loop COMMAND smoke_line_loop)
                        set_tests_properties(smoke_line_loop PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_orthpos tests/smoke_orthpos.c)
                        target_compile_definitions(smoke_orthpos PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_orthpos PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_orthpos ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_orthpos COMMAND smoke_orthpos)
                        set_tests_properties(smoke_orthpos PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_draw_batch tests/smoke_draw_batch.c)
                        target_compile_definitions(smoke_draw_batch PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_draw_batch PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_draw_batch ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_draw_batch COMMAND smoke_draw_batch)
                        set_tests_properties(smoke_draw_batch PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_long_primitives tests/smoke_long_primitives.c)
                        target_compile_definitions(smoke_long_primitives PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_long_primitives PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_long_primitives ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_long_primitives COMMAND smoke_long_primitives)
                        set_tests_properties(smoke_long_primitives PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_dlist_shademodel tests/smoke_dlist_shademodel.c)
                        target_compile_definitions(smoke_dlist_shademodel PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_dlist_shademodel PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_dlist_shademodel ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_dlist_shademodel COMMAND smoke_dlist_shademodel)
                        set_tests_properties(smoke_dlist_shademodel PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_arraybuffer_heal tests/smoke_arraybuffer_heal.c)
                        target_compile_definitions(smoke_arraybuffer_heal PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_arraybuffer_heal PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_arraybuffer_heal ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_arraybuffer_heal COMMAND smoke_arraybuffer_heal)
                        set_tests_properties(smoke_arraybuffer_heal PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_element_binding_survives tests/smoke_element_binding_survives.c)
                        target_compile_definitions(smoke_element_binding_survives PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_element_binding_survives PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_element_binding_survives ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_element_binding_survives COMMAND smoke_element_binding_survives)
                        set_tests_properties(smoke_element_binding_survives PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_uniform_cache tests/smoke_uniform_cache.c)
                        target_compile_definitions(smoke_uniform_cache PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_uniform_cache PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_uniform_cache ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_uniform_cache COMMAND smoke_uniform_cache)
                        set_tests_properties(smoke_uniform_cache PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_texgen_toggle tests/smoke_texgen_toggle.c)
                        target_compile_definitions(smoke_texgen_toggle PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_texgen_toggle PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_texgen_toggle ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_texgen_toggle COMMAND smoke_texgen_toggle)
                        set_tests_properties(smoke_texgen_toggle PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_display_list_chunks tests/smoke_display_list_chunks.c)
                        target_compile_definitions(smoke_display_list_chunks PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_display_list_chunks PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_display_list_chunks ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_display_list_chunks COMMAND smoke_display_list_chunks)
                        set_tests_properties(smoke_display_list_chunks PROPERTIES SKIP_RETURN_CODE 77)
                        # The same groups on a backend without glMultiDrawArrays,
                        # in a mode that cannot fall back to the quad indices.
                        add_test(NAME smoke_display_list_chunks_no_multidrawarrays
                                 COMMAND smoke_display_list_chunks triangles)
                        set_tests_properties(smoke_display_list_chunks_no_multidrawarrays PROPERTIES
                                SKIP_RETURN_CODE 77
                                ENVIRONMENT "SFPEW_NO_MULTIDRAWARRAYS=1")

                        add_executable(smoke_manual_mipgen tests/smoke_manual_mipgen.c)
                        target_compile_definitions(smoke_manual_mipgen PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_manual_mipgen PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_manual_mipgen ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_manual_mipgen COMMAND smoke_manual_mipgen)
                        set_tests_properties(smoke_manual_mipgen PROPERTIES SKIP_RETURN_CODE 77)
                        # The blit chain alone must produce every level: the
                        # "only" hook skips the backend mipgen, simulating the
                        # driver that leaves high levels stale.
                        add_test(NAME smoke_manual_mipgen_only COMMAND smoke_manual_mipgen)
                        set_tests_properties(smoke_manual_mipgen_only PROPERTIES
                                SKIP_RETURN_CODE 77
                                ENVIRONMENT "SFPEW_MANUAL_MIPGEN=only")

                        add_executable(smoke_bgra_texture tests/smoke_bgra_texture.c)
                        target_compile_definitions(smoke_bgra_texture PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_bgra_texture PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_bgra_texture ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_bgra_texture COMMAND smoke_bgra_texture)
                        set_tests_properties(smoke_bgra_texture PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_bgra_color_array tests/smoke_bgra_color_array.c)
                        target_compile_definitions(smoke_bgra_color_array PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_bgra_color_array PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_bgra_color_array ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_bgra_color_array COMMAND smoke_bgra_color_array)
                        set_tests_properties(smoke_bgra_color_array PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_mixed_translation_link tests/smoke_mixed_translation_link.c)
                        target_compile_definitions(smoke_mixed_translation_link PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_mixed_translation_link PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_mixed_translation_link ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_mixed_translation_link COMMAND smoke_mixed_translation_link)
                        set_tests_properties(smoke_mixed_translation_link PROPERTIES SKIP_RETURN_CODE 77)

                        add_executable(smoke_ffp_vbo_arrays tests/smoke_ffp_vbo_arrays.c)
                        target_compile_definitions(smoke_ffp_vbo_arrays PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_ffp_vbo_arrays PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_ffp_vbo_arrays ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_ffp_vbo_arrays COMMAND smoke_ffp_vbo_arrays)
                        set_tests_properties(smoke_ffp_vbo_arrays PROPERTIES SKIP_RETURN_CODE 77)

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
