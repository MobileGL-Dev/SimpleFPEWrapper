cmake_minimum_required(VERSION 3.22.1)

project("SimpleFPEWrapper")

enable_language(CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Executables are linked -static-libstdc++ below so one can run away from the
# toolchain that built it. That needs the static library to exist, which it
# does not on every machine able to build the wrapper - and until the test
# suites became C++, nothing here linked a C++ executable, so a missing one
# was invisible. Ask before asking for it.
find_library(SFPEW_LIBSTDCXX_STATIC NAMES libstdc++.a)

if(APPLE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default -funwind-tables -g -D_THREAD_SAFE -fPIC -stdlib=libc++")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -std=gnu99 -funwind-tables -O3 -fvisibility=hidden")
        if(SFPEW_LIBSTDCXX_STATIC)
                set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++")
        endif()
else()
        # FCL's `fordebug` variant intentionally keeps symbols and Java-side
        # debugging enabled, but AGP otherwise compiles this native library at
        # Clang's default -O0.  The fixed-function wrapper runs on every draw,
        # so an unoptimized C++ build dominates frame time on Android.  Keep
        # debug information while compiling the native hot path as optimized
        # production code.
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fno-semantic-interposition -funwind-tables -g -O3 -fno-math-errno -DNDEBUG -D_THREAD_SAFE -fPIC")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -std=gnu99 -funwind-tables -O3 -fvisibility=hidden")
        if(SFPEW_LIBSTDCXX_STATIC)
                set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++")
        endif()
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

# The git commit this build came from, reported alongside the version in
# GL_VERSION and logged once at initialization, so a device report always
# identifies the code it was produced by - a version cannot distinguish two
# builds made the same day. Configure time is good enough: the hash marks the
# checkout. The bare hash is the whole stamp; a working-tree state marker
# does not belong in a string users read and quote. Falls back to "unknown"
# outside a git checkout (tarball builds).
find_package(Git QUIET)
set(SFPEW_GIT_COMMIT "unknown")
if(GIT_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_SOURCE_DIR} rev-parse --short=12 HEAD
                        OUTPUT_VARIABLE SFPEW_GIT_COMMIT_RAW
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET RESULT_VARIABLE SFPEW_GIT_RC)
        if(SFPEW_GIT_RC EQUAL 0)
                set(SFPEW_GIT_COMMIT "${SFPEW_GIT_COMMIT_RAW}")
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
# GoogleTest, vendored like every other dependency; its install rules and
# gmock are of no use here. The suites are the same source on either side:
# the host runs them through ctest, the device gets them pushed.
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/googletest EXCLUDE_FROM_ALL)
include(GoogleTest)

# Every test case runs in its own process, and has to: the wrapper resolves
# its backend once per process and caches the identity strings, the
# desktop-or-GLES decision and the MobileGlues probe behind function-local
# statics. Two cases sharing a process would have the second one inherit the
# first one's backend. gtest_discover_tests gives exactly that - one ctest
# per TEST, each a fresh --gtest_filter run - which is why the suites are
# registered through it and never through a bare add_test on the binary.
#
# Running a suite binary directly still runs its cases in one process. That
# is fine within one backend and wrong across them; ctest is the supported
# way to run these, and on the device --gtest_filter is.
function(sfpew_add_gtest name)
        cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES" ${ARGN})
        add_executable(${name} ${ARG_SOURCES})
        # On the device the library sits next to the binary; on the host it
        # is wherever this build put it.
        if(ANDROID)
                target_compile_definitions(${name} PRIVATE
                        WRAPPER_LIB_PATH="lib${CMAKE_PROJECT_NAME}.so")
        else()
                target_compile_definitions(${name} PRIVATE
                        WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
        endif()
        target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
        target_link_libraries(${name} PRIVATE GTest::gtest_main dl ${ARG_LIBRARIES})
        add_dependencies(${name} ${CMAKE_PROJECT_NAME})
        # Discovery runs the binary, which a cross build cannot do.
        if(NOT ANDROID)
                # A driver that has to spin up a context per case is not
                # fast; llvmpipe needs the room.
                gtest_discover_tests(${name} DISCOVERY_TIMEOUT 120 PROPERTIES TIMEOUT 300)
        endif()
endfunction()

if(ANDROID)
        target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE log)

        # The state sweeps, built for the device as well. Everything else is
        # verified against Mesa on the host, but which state a driver can be
        # asked for is exactly what differs between one GLES stack and the
        # next - and the stack this ships on is not the one CI runs. They are
        # pushed and run with --gtest_filter one case at a time, since the
        # device has no ctest to keep the cases in separate processes.
        foreach(suite version ffp_getters)
                sfpew_add_gtest(sfpew_${suite}_test SOURCES tests/gtest_${suite}.cc
                                LIBRARIES EGL)
        endforeach()
endif()

# Host-side tests (not applicable to Android cross builds).
if(NOT ANDROID)
        include(CTest)
        if(BUILD_TESTING)
                # Every test case runs in its own process, and has to: the
                # wrapper resolves its backend once per process and caches the
                # identity strings, the desktop-or-GLES decision and the
                # MobileGlues probe behind function-local statics. Two cases
                # sharing a process would have the second one inherit the
                # first one's backend. gtest_discover_tests gives exactly
                # that - one ctest per TEST, each a fresh --gtest_filter run -
                # which is why the suites below are registered through it and
                # never through a bare add_test on the binary.
                #
                # Running a suite binary directly still runs its cases in one
                # process. That is fine within one backend and wrong across
                # them; ctest is the supported way to run these.

                # Migrated suites. Each file is one area of behaviour; the
                # backend a case needs is chosen by its fixture. These need no
                # driver at all - the wrapper's own bookkeeping.
                foreach(suite load error display_list_cpu)
                        sfpew_add_gtest(sfpew_${suite}_test SOURCES tests/gtest_${suite}.cc)
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
                        # Suites that need a driver underneath.
                        foreach(suite version ffp_getters empty_beginend edgeflag line_loop
                                       degenerate_prims long_primitives immediate_merge
                                       dlist_shademodel dlist_beginend dlist_no_state_leak
                                       list_current_attr ffp_vbo_arrays bgra_color_array
                                       attrib_retire arraybuffer_heal element_binding_survives
                                       buffer_name_reuse display_list_chunks fog rastercolor
                                       orthpos texgen_toggle polygon_mode draw_batch
                                       swap_flushes_batch list_immediate lwjgl_surface
                                       uniform_cache userprog_enable_cache userobject_blit
                                       userprog_multiattrib userobject_quads gl3_core
                                       bgra_texture manual_mipgen)
                                sfpew_add_gtest(sfpew_${suite}_test SOURCES tests/gtest_${suite}.cc
                                                LIBRARIES ${SFPEW_TEST_EGL})
                        endforeach()
                        # The translator feeds its output to the REAL driver's
                        # glCompileShader, so it links the system GLES too.
                        sfpew_add_gtest(sfpew_translator_test SOURCES tests/gtest_translator.cc
                                        LIBRARIES ${SFPEW_TEST_EGL} ${SFPEW_TEST_GLES})




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








                        add_executable(smoke_beginend_coverage tests/smoke_beginend_coverage.c)
                        target_compile_definitions(smoke_beginend_coverage PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_beginend_coverage PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_beginend_coverage ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_beginend_coverage COMMAND smoke_beginend_coverage)
                        set_tests_properties(smoke_beginend_coverage PROPERTIES SKIP_RETURN_CODE 77)






                        add_executable(smoke_mixed_translation_link tests/smoke_mixed_translation_link.c)
                        target_compile_definitions(smoke_mixed_translation_link PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_mixed_translation_link PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_mixed_translation_link ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_mixed_translation_link COMMAND smoke_mixed_translation_link)
                        set_tests_properties(smoke_mixed_translation_link PROPERTIES SKIP_RETURN_CODE 77)



                        add_executable(smoke_vbo_surface tests/smoke_vbo_surface.c)
                        target_compile_definitions(smoke_vbo_surface PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_vbo_surface PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_vbo_surface ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_vbo_surface COMMAND smoke_vbo_surface)
                        set_tests_properties(smoke_vbo_surface PROPERTIES SKIP_RETURN_CODE 77)



                        add_executable(smoke_render tests/smoke_render.c)
                        target_compile_definitions(smoke_render PRIVATE
                                WRAPPER_LIB_PATH="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                        target_link_libraries(smoke_render PRIVATE dl ${SFPEW_TEST_EGL})
                        add_dependencies(smoke_render ${CMAKE_PROJECT_NAME})
                        add_test(NAME smoke_render COMMAND smoke_render)
                        set_tests_properties(smoke_render PROPERTIES SKIP_RETURN_CODE 77)

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
