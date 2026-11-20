CPMAddPackage(
        NAME Catch2
        GITHUB_REPOSITORY catchorg/Catch2
        GIT_TAG v3.15.0
        OPTIONS
        "CATCH_INSTALL_DOCS OFF"
        "CATCH_INSTALL_EXTRAS OFF"
)

function(test filename)
    set(target_name test.${filename})
    add_executable(${target_name})

    target_sources(${target_name} PRIVATE ${filename}.cpp)

    target_link_libraries(${target_name} PRIVATE CxxRelay::CxxRelay)
    target_link_libraries(${target_name} PRIVATE Catch2::Catch2WithMain)

    add_test(NAME ${target_name} COMMAND $<TARGET_FILE:${target_name}>)
endfunction()

## Unit tests (alphabetical order)
##
test("chat")
test("core")
test("invocation")
test("json")
test("method")
test("tool")
test("value")
