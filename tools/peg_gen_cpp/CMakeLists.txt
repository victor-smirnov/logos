add_executable(peg_gen_cpp
    src/main.cpp
    src/grammar_parser.cpp
    src/module_resolver.cpp
    src/codegen.cpp
)

target_link_libraries(peg_gen_cpp PRIVATE
    logos_writ  # peg_gen_cpp uses writ internally for the grammar AST
    logos_core
)

target_include_directories(peg_gen_cpp PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    src
)

add_executable(peg_gen_cpp_test
    test/peg_gen_test.cpp
    src/grammar_parser.cpp
    src/module_resolver.cpp
    src/codegen.cpp
)

target_link_libraries(peg_gen_cpp_test PRIVATE
    logos_writ
    logos_core
)

target_include_directories(peg_gen_cpp_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    src
)

target_compile_definitions(peg_gen_cpp_test PRIVATE
    PEGEN_GRAMMARS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/grammars"
)

# ── Roundtrip test: generate writ_parser then compile and run it ────────────

set(WRIT_PARSER_HPP ${CMAKE_CURRENT_BINARY_DIR}/writ_parser.hpp)
set(WRIT_PARSER_CPP ${CMAKE_CURRENT_BINARY_DIR}/writ_parser.cpp)

add_custom_command(
    OUTPUT  ${WRIT_PARSER_HPP} ${WRIT_PARSER_CPP}
    COMMAND $<TARGET_FILE:peg_gen_cpp>
            ${CMAKE_CURRENT_SOURCE_DIR}/grammars/writ.peg
            --out-dir ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS peg_gen_cpp ${CMAKE_CURRENT_SOURCE_DIR}/grammars/writ.peg
    COMMENT "Generating writ_parser from writ.peg"
)

add_executable(peg_gen_cpp_roundtrip
    test/roundtrip_test.cpp
    ${WRIT_PARSER_CPP}
)

target_include_directories(peg_gen_cpp_roundtrip PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}   # generated writ_parser.hpp
)

target_link_libraries(peg_gen_cpp_roundtrip PRIVATE logos_writ logos_core)
# Generated parser has unused-variable warnings for positional TOKEN_REF captures
# that exist in the grammar but are not referenced in actions (structural punctuation).
set_source_files_properties(${WRIT_PARSER_CPP} PROPERTIES
    COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-but-set-variable"
)

# Install bundled grammars under the project share dir (/usr/share/logos/peg_gen_cpp
# with the package prefix). Reference/tooling data — the parser is generated
# into logosc at build time, so nothing reads this at runtime.
install(DIRECTORY grammars/ DESTINATION share/logos/peg_gen_cpp)
