add_library(logos_hermes2
    arena.cpp
    clone.cpp
)

target_include_directories(logos_hermes2 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
)

target_link_libraries(logos_hermes2 PUBLIC
    logos_verification
)

# Exerciser: Layer 0 (foundation — relative_ptr, arena, any_val)
add_executable(hermes2_exerciser_smoke exerciser_smoke.cpp)
target_link_libraries(hermes2_exerciser_smoke PRIVATE logos_hermes2)
add_test(NAME hermes2_exerciser_smoke COMMAND hermes2_exerciser_smoke)
set_tests_properties(hermes2_exerciser_smoke PROPERTIES LABELS "hermes2;cpp")

# Exerciser: Layer 2 (containers — ArenaString, ObjectArray)
add_executable(hermes2_exerciser_containers exerciser_containers.cpp)
target_link_libraries(hermes2_exerciser_containers PRIVATE logos_hermes2)
add_test(NAME hermes2_exerciser_containers COMMAND hermes2_exerciser_containers)
set_tests_properties(hermes2_exerciser_containers PROPERTIES LABELS "hermes2;cpp")

# Exerciser: Layer 3 (owning views + MemHolder refcount)
add_executable(hermes2_exerciser_views exerciser_views.cpp)
target_link_libraries(hermes2_exerciser_views PRIVATE logos_hermes2)
add_test(NAME hermes2_exerciser_views COMMAND hermes2_exerciser_views)
set_tests_properties(hermes2_exerciser_views PROPERTIES LABELS "hermes2;cpp")

# Exerciser: Layer 4 (clone / compaction — deep_copy + dedup)
add_executable(hermes2_exerciser_clone exerciser_clone.cpp)
target_link_libraries(hermes2_exerciser_clone PRIVATE logos_hermes2)
add_test(NAME hermes2_exerciser_clone COMMAND hermes2_exerciser_clone)
set_tests_properties(hermes2_exerciser_clone PROPERTIES LABELS "hermes2;cpp")

# Exerciser: Layer 5 (document + compactify-to-blob + reload)
add_executable(hermes2_exerciser_document exerciser_document.cpp)
target_link_libraries(hermes2_exerciser_document PRIVATE logos_hermes2)
add_test(NAME hermes2_exerciser_document COMMAND hermes2_exerciser_document)
set_tests_properties(hermes2_exerciser_document PROPERTIES LABELS "hermes2;cpp")
