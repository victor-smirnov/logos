find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)

# ---------------------------------------------------------------------------
# logos_reactor — core library (fiber context switch + scheduler + reactor)
# ---------------------------------------------------------------------------
add_library(logos_reactor STATIC
    fiber_switch.S
    fiber.cpp
    scheduler.cpp
    reactor.cpp
)

target_include_directories(logos_reactor PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${LIBURING_INCLUDE_DIRS}
)

target_link_libraries(logos_reactor PUBLIC
    logos_verification
    ${LIBURING_LIBRARIES}
)

# ---------------------------------------------------------------------------
# Layer 0 exerciser — fibers + scheduler only (no io_uring)
# ---------------------------------------------------------------------------
add_executable(reactor_exerciser_fibers exerciser_fibers.cpp)
target_link_libraries(reactor_exerciser_fibers PRIVATE logos_reactor)

# ---------------------------------------------------------------------------
# Layer 1 exerciser — reactor + io_uring sleep
# ---------------------------------------------------------------------------
add_executable(reactor_exerciser_reactor exerciser_reactor.cpp)
target_link_libraries(reactor_exerciser_reactor PRIVATE logos_reactor)

# ---------------------------------------------------------------------------
# Layer 2 exerciser — mutex / condition_variable / channel
# ---------------------------------------------------------------------------
add_executable(reactor_exerciser_sync exerciser_sync.cpp)
target_link_libraries(reactor_exerciser_sync PRIVATE logos_reactor)

# ---------------------------------------------------------------------------
# Layer 3 exerciser — async file IO (io_uring read/write)
# ---------------------------------------------------------------------------
add_executable(reactor_exerciser_file exerciser_file.cpp)
target_link_libraries(reactor_exerciser_file PRIVATE logos_reactor)

# ---------------------------------------------------------------------------
# Layer 4 exerciser — TCP networking (io_uring accept/connect/recv/send)
# ---------------------------------------------------------------------------
add_executable(reactor_exerciser_net exerciser_net.cpp)
target_link_libraries(reactor_exerciser_net PRIVATE logos_reactor)

# ---------------------------------------------------------------------------
# Layer 5 exerciser — Unix domain sockets
# ---------------------------------------------------------------------------
add_executable(reactor_exerciser_uds exerciser_uds.cpp)
target_link_libraries(reactor_exerciser_uds PRIVATE logos_reactor)

# ---------------------------------------------------------------------------
# Layer 6 exerciser — UDP (io_uring sendmsg/recvmsg)
# ---------------------------------------------------------------------------
add_executable(reactor_exerciser_udp exerciser_udp.cpp)
target_link_libraries(reactor_exerciser_udp PRIVATE logos_reactor)

# ---------------------------------------------------------------------------
# Layer 7 exerciser — signal handling + graceful shutdown
# ---------------------------------------------------------------------------
add_executable(reactor_exerciser_signals exerciser_signals.cpp)
target_link_libraries(reactor_exerciser_signals PRIVATE logos_reactor)
