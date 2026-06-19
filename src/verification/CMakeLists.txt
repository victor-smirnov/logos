add_library(logos_verification
    call_chain.cpp
    failure_handler.cpp
)

target_include_directories(logos_verification PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
)

target_link_libraries(logos_verification PUBLIC
    ${CMAKE_DL_LIBS}
)
