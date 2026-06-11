find_package(PkgConfig REQUIRED)

# ---------------------------------------------------------------------------
# logos_hrpc — HRPC session library
# ---------------------------------------------------------------------------
add_library(logos_hrpc STATIC session.cpp)

target_include_directories(logos_hrpc PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(logos_hrpc PUBLIC
    logos_reactor
    logos_hermes2
    logos_verification
)

# ---------------------------------------------------------------------------
# Layer 0 exerciser — basic request-response RPC
# ---------------------------------------------------------------------------
add_executable(hrpc_exerciser_session exerciser_session.cpp)
target_link_libraries(hrpc_exerciser_session PRIVATE logos_hrpc)

# ---------------------------------------------------------------------------
# Layer 1 exerciser — streaming RPC
# ---------------------------------------------------------------------------
add_executable(hrpc_exerciser_streaming exerciser_streaming.cpp)
target_link_libraries(hrpc_exerciser_streaming PRIVATE logos_hrpc)

# ---------------------------------------------------------------------------
# Layer 2 exerciser — local cross-reactor RPC (no TCP)
# ---------------------------------------------------------------------------
add_executable(hrpc_exerciser_local exerciser_local.cpp)
target_link_libraries(hrpc_exerciser_local PRIVATE logos_hrpc)
