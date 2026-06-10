add_library(logos_hermes2
    arena.cpp
    clone.cpp
    binary_codec.cpp
    stringify.cpp
    text_parser.cpp
    arena_pool.cpp
    external_ref.cpp
    import_table.cpp
    arena_publish.cpp
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

# Exerciser: Layer 6 (binary codec — encode/decode roundtrip)
add_executable(hermes2_exerciser_codec exerciser_codec.cpp)
target_link_libraries(hermes2_exerciser_codec PRIVATE logos_hermes2)
add_test(NAME hermes2_exerciser_codec COMMAND hermes2_exerciser_codec)
set_tests_properties(hermes2_exerciser_codec PROPERTIES LABELS "hermes2;cpp")

# Exerciser: Layer 7 (text — stringify + parse roundtrip)
add_executable(hermes2_exerciser_text exerciser_text.cpp)
target_link_libraries(hermes2_exerciser_text PRIVATE logos_hermes2)
add_test(NAME hermes2_exerciser_text COMMAND hermes2_exerciser_text)
set_tests_properties(hermes2_exerciser_text PROPERTIES LABELS "hermes2;cpp")

# Exerciser: Layer 8 (multi-arena — ExternalRef niche, arena pool, publish/resolve, import table)
add_executable(hermes2_exerciser_multi_arena exerciser_multi_arena.cpp)
target_link_libraries(hermes2_exerciser_multi_arena PRIVATE logos_hermes2)
add_test(NAME hermes2_exerciser_multi_arena COMMAND hermes2_exerciser_multi_arena)
set_tests_properties(hermes2_exerciser_multi_arena PROPERTIES LABELS "hermes2;cpp")
