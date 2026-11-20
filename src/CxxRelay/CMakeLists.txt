add_library(CxxRelay STATIC
        src/chat.cpp
        src/core.cpp
        src/curl.cpp
        src/json.cpp
        src/json_schema.cpp
        src/openai_responses.cpp
        src/tool_dispatcher.cpp
        src/value.cpp
)
add_library(CxxRelay::CxxRelay ALIAS CxxRelay)

target_include_directories(CxxRelay PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

## Dependencies
##
CPMAddPackage(
        NAME curl
        GITHUB_REPOSITORY curl/curl
        GIT_TAG curl-8_22_0
)

target_link_libraries(CxxRelay PRIVATE CURL::libcurl)
target_link_libraries(CxxRelay PRIVATE CxxRelay::enable_compiler_warnings)

## Tests
##
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
