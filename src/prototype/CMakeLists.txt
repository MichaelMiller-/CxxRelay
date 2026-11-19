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