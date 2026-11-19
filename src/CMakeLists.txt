add_subdirectory(CxxRelay)
add_subdirectory(prototype)

if(CXX_RELAY_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
