add_library(logos_hermes
    arena.cpp
    document.cpp
    view.cpp
    binary_codec.cpp
    text_parser.cpp
    stringify.cpp
    path.cpp
    template.cpp
)

target_include_directories(logos_hermes PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
)

target_link_libraries(logos_hermes PUBLIC
    logos_verification
)

# Exerciser: Layer 0 (arena primitives)
add_executable(hermes_exerciser_arena exerciser_arena.cpp)
target_link_libraries(hermes_exerciser_arena PRIVATE logos_hermes)

# Exerciser: Layer 1 (type system & core datatypes)
add_executable(hermes_exerciser_types exerciser_types.cpp)
target_link_libraries(hermes_exerciser_types PRIVATE logos_hermes)

# Exerciser: Layer 2 (containers)
add_executable(hermes_exerciser_containers exerciser_containers.cpp)
target_link_libraries(hermes_exerciser_containers PRIVATE logos_hermes)

# Exerciser: Layer 3-4 (document, serialization)
add_executable(hermes_exerciser_document exerciser_document.cpp)
target_link_libraries(hermes_exerciser_document PRIVATE logos_hermes)

# Exerciser: deep copy + binary codec
add_executable(hermes_exerciser_codec exerciser_codec.cpp)
target_link_libraries(hermes_exerciser_codec PRIVATE logos_hermes)

# Exerciser: text parser + stringify
add_executable(hermes_exerciser_text exerciser_text.cpp)
target_link_libraries(hermes_exerciser_text PRIVATE logos_hermes)

# Exerciser: HermesPath + Template
add_executable(hermes_exerciser_path exerciser_path.cpp)
target_link_libraries(hermes_exerciser_path PRIVATE logos_hermes)

# Walkthrough: interactive API tour for debugging
add_executable(hermes_walkthrough walkthrough.cpp)
target_link_libraries(hermes_walkthrough PRIVATE logos_hermes)
