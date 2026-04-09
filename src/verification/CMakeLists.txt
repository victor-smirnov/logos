add_library(logos_verification
    call_chain.cpp
    sqlite_sink.cpp
    failure_handler.cpp
)

target_include_directories(logos_verification PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
)

target_link_libraries(logos_verification PUBLIC
    PkgConfig::SQLite3
    ${CMAKE_DL_LIBS}
)

# Optional: Add a test executable
add_executable(verification_test test_main.cpp)
target_link_libraries(verification_test PRIVATE logos_verification ${CMAKE_DL_LIBS})
target_compile_options(verification_test PRIVATE -finstrument-functions)
target_link_options(verification_test PRIVATE -rdynamic)
