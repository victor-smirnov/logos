add_executable(peg_gen
    src/main.cpp
    src/grammar_parser.cpp
    src/module_resolver.cpp
    src/codegen.cpp
)

target_link_libraries(peg_gen PRIVATE
    logos_hermes   # peg_gen uses Hermes internally for grammar AST
    logos_core
)

target_include_directories(peg_gen PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    src
)

add_executable(peg_gen_test
    test/peg_gen_test.cpp
    src/grammar_parser.cpp
    src/module_resolver.cpp
    src/codegen.cpp
)

target_link_libraries(peg_gen_test PRIVATE
    logos_hermes
    logos_core
)

target_include_directories(peg_gen_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    src
)

target_compile_definitions(peg_gen_test PRIVATE
    PEGEN_GRAMMARS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/grammars"
)

# ── Roundtrip test: generate hermes_parser then compile and run it ────────────

set(HERMES_PARSER_HPP ${CMAKE_CURRENT_BINARY_DIR}/hermes_parser.hpp)
set(HERMES_PARSER_CPP ${CMAKE_CURRENT_BINARY_DIR}/hermes_parser.cpp)

add_custom_command(
    OUTPUT  ${HERMES_PARSER_HPP} ${HERMES_PARSER_CPP}
    COMMAND $<TARGET_FILE:peg_gen>
            ${CMAKE_CURRENT_SOURCE_DIR}/grammars/hermes.peg
            --out-dir ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS peg_gen ${CMAKE_CURRENT_SOURCE_DIR}/grammars/hermes.peg
    COMMENT "Generating hermes_parser from hermes.peg"
)

add_executable(peg_gen_roundtrip
    test/roundtrip_test.cpp
    ${HERMES_PARSER_CPP}
)

target_include_directories(peg_gen_roundtrip PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}   # generated hermes_parser.hpp
)

target_link_libraries(peg_gen_roundtrip PRIVATE logos_hermes logos_core)
# Generated parser has unused-variable warnings for positional TOKEN_REF captures
# that exist in the grammar but are not referenced in actions (structural punctuation).
set_source_files_properties(${HERMES_PARSER_CPP} PROPERTIES
    COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-but-set-variable"
)

# Install bundled grammars alongside the executable.
install(DIRECTORY grammars/ DESTINATION share/peg_gen/grammars)
