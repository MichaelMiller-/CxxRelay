function(example filename)
    set(target_name example.${filename})
    add_executable(${target_name})

    target_sources(${target_name} PRIVATE ${filename}.cpp)

    target_link_libraries(${target_name} PRIVATE CxxRelay::CxxRelay)
    target_link_libraries(${target_name} PRIVATE CxxRelay::enable_compiler_warnings)
endfunction()

## Examples (ascending complexity)
##
example("value")
example("core")
example("method")
example("invocation")
example("json")
example("tool")
example("chat")
example("openai")
