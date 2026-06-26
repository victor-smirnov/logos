add_library(logos_writ
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

target_include_directories(logos_writ PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
)

target_link_libraries(logos_writ PUBLIC
    logos_verification
)

# Exerciser: Layer 0 (foundation — relative_ptr, arena, any_val)
add_executable(writ_exerciser_smoke exerciser_smoke.cpp)
target_link_libraries(writ_exerciser_smoke PRIVATE logos_writ)
add_test(NAME writ_exerciser_smoke COMMAND writ_exerciser_smoke)
set_tests_properties(writ_exerciser_smoke PROPERTIES LABELS "writ;cpp")

# Exerciser: Layer 2 (containers — ArenaString, ObjectArray)
add_executable(writ_exerciser_containers exerciser_containers.cpp)
target_link_libraries(writ_exerciser_containers PRIVATE logos_writ)
add_test(NAME writ_exerciser_containers COMMAND writ_exerciser_containers)
set_tests_properties(writ_exerciser_containers PROPERTIES LABELS "writ;cpp")

# Exerciser: Layer 3 (owning views + MemHolder refcount)
add_executable(writ_exerciser_views exerciser_views.cpp)
target_link_libraries(writ_exerciser_views PRIVATE logos_writ)
add_test(NAME writ_exerciser_views COMMAND writ_exerciser_views)
set_tests_properties(writ_exerciser_views PROPERTIES LABELS "writ;cpp")

# Exerciser: Layer 4 (clone / compaction — deep_copy + dedup)
add_executable(writ_exerciser_clone exerciser_clone.cpp)
target_link_libraries(writ_exerciser_clone PRIVATE logos_writ)
add_test(NAME writ_exerciser_clone COMMAND writ_exerciser_clone)
set_tests_properties(writ_exerciser_clone PROPERTIES LABELS "writ;cpp")

# Exerciser: Layer 5 (document + compactify-to-blob + reload)
add_executable(writ_exerciser_document exerciser_document.cpp)
target_link_libraries(writ_exerciser_document PRIVATE logos_writ)
add_test(NAME writ_exerciser_document COMMAND writ_exerciser_document)
set_tests_properties(writ_exerciser_document PROPERTIES LABELS "writ;cpp")

# Exerciser: Layer 6 (binary codec — encode/decode roundtrip)
add_executable(writ_exerciser_codec exerciser_codec.cpp)
target_link_libraries(writ_exerciser_codec PRIVATE logos_writ)
add_test(NAME writ_exerciser_codec COMMAND writ_exerciser_codec)
set_tests_properties(writ_exerciser_codec PROPERTIES LABELS "writ;cpp")

# Exerciser: Layer 7 (text — stringify + parse roundtrip)
add_executable(writ_exerciser_text exerciser_text.cpp)
target_link_libraries(writ_exerciser_text PRIVATE logos_writ)
add_test(NAME writ_exerciser_text COMMAND writ_exerciser_text)
set_tests_properties(writ_exerciser_text PROPERTIES LABELS "writ;cpp")

# Exerciser: Layer 8 (multi-arena — ExternalRef niche, arena pool, publish/resolve, import table)
add_executable(writ_exerciser_multi_arena exerciser_multi_arena.cpp)
target_link_libraries(writ_exerciser_multi_arena PRIVATE logos_writ)
add_test(NAME writ_exerciser_multi_arena COMMAND writ_exerciser_multi_arena)
set_tests_properties(writ_exerciser_multi_arena PROPERTIES LABELS "writ;cpp")

# Exerciser: Layer 9 (producer — AST-like build via make_doc/make_tiny_map + schema_type_code)
add_executable(writ_exerciser_producer exerciser_producer.cpp)
target_link_libraries(writ_exerciser_producer PRIVATE logos_writ)
add_test(NAME writ_exerciser_producer COMMAND writ_exerciser_producer)
set_tests_properties(writ_exerciser_producer PROPERTIES LABELS "writ;cpp")
