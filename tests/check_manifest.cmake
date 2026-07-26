# Regenerates the API manifest into the build tree and fails if the
# committed docs/api-manifest.json is stale.
execute_process(COMMAND ${PY} ${SRC}/tools/gen_api_manifest.py ${SRC} ${BIN}/manifest
                RESULT_VARIABLE gen_result)
if(NOT gen_result EQUAL 0)
        message(FATAL_ERROR "manifest generation failed")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files
                ${BIN}/manifest/api-manifest.json ${SRC}/docs/api-manifest.json
                RESULT_VARIABLE diff_result)
if(NOT diff_result EQUAL 0)
        message(FATAL_ERROR "docs/api-manifest.json is stale: run tools/gen_api_manifest.py <repo> <repo>/docs")
endif()
