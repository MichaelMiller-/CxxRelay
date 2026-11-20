add_subdirectory(CxxRelay)
add_subdirectory(prototype)

if(CXXRELAY_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
