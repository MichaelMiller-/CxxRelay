## Proof-of-concept worker
##
add_executable(prototype.worker)

target_sources(prototype.worker
        PRIVATE
        worker/mock_provider.h
        worker/tracing_provider.h
        worker/worker.cpp
)

target_link_libraries(prototype.worker PRIVATE CxxRelay::CxxRelay)

##
configure_file(run.sh.in ${CMAKE_CURRENT_BINARY_DIR}/run.sh @ONLY)


set(CXXRELAY_PROTOTYPE_MODE "mock" CACHE STRING "Prototype mode: mock or openai")
set_property(CACHE CXXRELAY_PROTOTYPE_MODE
        PROPERTY STRINGS mock openai
)

set(CXXRELAY_PROTOTYPE_OPENAI_API_KEY "" CACHE STRING "OpenAI API key for the chat prototype")
set(CXXRELAY_PROTOTYPE_OPENAI_MODEL "" CACHE STRING "OpenAI model for the chat prototype")


if(NOT CXXRELAY_PROTOTYPE_MODE STREQUAL "mock" AND NOT CXXRELAY_PROTOTYPE_MODE STREQUAL "openai")
    message(FATAL_ERROR "Invalid CXXRELAY_PROTOTYPE_MODE=${CXXRELAY_PROTOTYPE_MODE}. Choose mock or openai.")
endif()

set(_cxxrelay_prototype_env)

if(CXXRELAY_PROTOTYPE_MODE STREQUAL "openai")
    # Allow environment variables as a safer alternative to storing API keys in the CMake cache.
    if("${CXXRELAY_PROTOTYPE_OPENAI_API_KEY}" STREQUAL "" AND "$ENV{OPENAI_API_KEY}" STREQUAL "")
        message(FATAL_ERROR "OpenAI mode requires CXXRELAY_PROTOTYPE_OPENAI_API_KEY or the OPENAI_API_KEY environment variable.")
    endif()

    string(STRIP "${CXXRELAY_PROTOTYPE_OPENAI_MODEL}" _cxxrelay_model)

    if(_cxxrelay_model STREQUAL "")
        message(FATAL_ERROR "OpenAI mode requires CXXRELAY_PROTOTYPE_OPENAI_MODEL."
        )
    endif()

    list(APPEND _cxxrelay_prototype_env "OPENAI_MODEL=${CXXRELAY_PROTOTYPE_OPENAI_MODEL}")
    list(APPEND _cxxrelay_prototype_env "OPENAI_API_KEY=${CXXRELAY_PROTOTYPE_OPENAI_API_KEY}")
endif()
# cmake_print_variables(_cxxrelay_prototype_env)


find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(prototype-python
            COMMAND ${CMAKE_COMMAND} -E env
                "PYTHONUNBUFFERED=1"
                ${_cxxrelay_prototype_env}
                ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_LIST_DIR}/server.py"
                --mode "${CXXRELAY_PROTOTYPE_MODE}"
                --worker "$<TARGET_FILE:prototype.worker>"
            DEPENDS prototype.worker
            USES_TERMINAL
            VERBATIM
            COMMENT "Starting CxxRelay Python prototype (${CXXRELAY_PROTOTYPE_MODE})"
    )
endif()

if(WIN32)
    add_custom_target(prototype-powershell
            COMMAND "${CMAKE_COMMAND}" -E env
                ${_cxxrelay_prototype_env}
                powershell.exe -NoLogo -NoProfile -STA
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_LIST_DIR}/start.ps1"
                -Mode "${CXXRELAY_PROTOTYPE_MODE}"
                -WorkerPath "$<TARGET_FILE:prototype.worker>"
            DEPENDS prototype.worker
            USES_TERMINAL
            VERBATIM
            COMMENT "Starting CxxRelay Windows Forms prototype (${CXXRELAY_PROTOTYPE_MODE})"
    )
endif()
