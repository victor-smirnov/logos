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

# ── The AST constants header is GENERATED from logos.peg ──────────────────────
# include/logos/compiler/ast.hpp used to restate the grammar's %fields/%nodes by
# hand. Nothing checked the two agreed; a name mismatch is a compile error, but a
# VALUE mismatch is a silent runtime bug — and one had already happened
# (WRIT_BLOB and TEMPLATE_DECL both claimed node code 199). The grammar is now
# the only declaration; the committed header is a generated artifact, and the
# test below fails the build if it drifts.
set(LOGOS_AST_HEADER ${CMAKE_SOURCE_DIR}/include/logos/compiler/ast.hpp)
set(LOGOS_PEG        ${CMAKE_CURRENT_SOURCE_DIR}/grammars/logos.peg)

add_custom_target(peg_ast_regen
    COMMAND $<TARGET_FILE:peg_gen_cpp> ${LOGOS_PEG}
            --out-dir ${CMAKE_CURRENT_BINARY_DIR}/ast_regen
            --ast-header ${LOGOS_AST_HEADER}
    DEPENDS peg_gen_cpp ${LOGOS_PEG}
    COMMENT "Regenerating include/logos/compiler/ast.hpp from logos.peg")

add_test(NAME peg_ast_header_fresh
    COMMAND ${CMAKE_COMMAND}
            -DPEG=$<TARGET_FILE:peg_gen_cpp>
            -DGRAMMAR=${LOGOS_PEG}
            -DHEADER=${LOGOS_AST_HEADER}
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/ast_fresh_check
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_ast_header.cmake)
set_tests_properties(peg_ast_header_fresh PROPERTIES LABELS "lint")

# ── The other mirror: %schema blocks vs the ADR-0011 `schema` items ──────────
# el.peg's %schema restates stdlib/mem/wql/ir.logos, wql.peg's restates
# plan.logos. The grammar declares only the fields it writes, and the .logos
# files also carry schema enums, helpers and the fan-cap fns — so the .peg
# cannot GENERATE the items. But every field it DOES declare must agree, or the
# generated parser writes the wrong TOM slot. Until now el.peg merely ASKED, in
# a comment, that "types MUST match the field types in ir.logos exactly".
set(WQL_GRAMMARS ${CMAKE_SOURCE_DIR}/stdlib/mem/wql/grammars)
set(WQL_SCHEMAS  ${CMAKE_SOURCE_DIR}/stdlib/mem/wql)

add_test(NAME peg_schema_mirror_el
    COMMAND $<TARGET_FILE:peg_gen_cpp> ${WQL_GRAMMARS}/el.peg
            --check-schema ${WQL_SCHEMAS}/ir.logos)
add_test(NAME peg_schema_mirror_wql
    COMMAND $<TARGET_FILE:peg_gen_cpp> ${WQL_GRAMMARS}/wql.peg
            --check-schema ${WQL_SCHEMAS}/plan.logos
            --check-schema ${WQL_SCHEMAS}/ir.logos)
set_tests_properties(peg_schema_mirror_el peg_schema_mirror_wql
    PROPERTIES LABELS "lint")

# EL cross-BACKEND oracle — the check that makes "one dialect, two backends" a
# fact rather than a hope: parse one EL corpus with the parser peg_gen_cpp
# generates from el.peg AND with the parser peg_gen_logos generates from the SAME
# el.peg, and require the stringified SExpr trees to be identical. Opt-in (it
# regenerates a parser and links two harnesses), mirroring peg_gen_logos_oracle.
#
#   cmake --build build --target peg_gen_cpp_el_oracle
add_custom_target(peg_gen_cpp_el_oracle
    COMMAND ${CMAKE_COMMAND} -E env bash
            ${CMAKE_CURRENT_SOURCE_DIR}/oracle/run.sh
            ${CMAKE_BINARY_DIR}
    DEPENDS logosc peg_gen_cpp
    USES_TERMINAL
    COMMENT "peg_gen_cpp EL oracle (C++ vs Logos el_parser over the EL corpus)")

# WQL cross-BACKEND oracle — the same check one level up, over deem!-body queries
# rather than bare expressions. Not redundant with the EL oracle: the INTEGER
# type-suffix divergence survived four grammars precisely because no EL corpus
# line ever wrote `1000000i64`, while nearly every real deem! body does. Since the
# zero-copy rule-IR seam, logosc parses rule bodies with the C++ parser while the
# runtime `Query::compile` still uses the Logos one — they must agree, or a query
# means different things at compile time and at run time.
#
#   cmake --build build --target peg_gen_cpp_wql_oracle
add_custom_target(peg_gen_cpp_wql_oracle
    COMMAND ${CMAKE_COMMAND} -E env bash
            ${CMAKE_CURRENT_SOURCE_DIR}/oracle/run_wql.sh
            ${CMAKE_BINARY_DIR}
    DEPENDS logosc peg_gen_cpp
    USES_TERMINAL
    COMMENT "peg_gen_cpp WQL oracle (C++ vs Logos wql_surface_parser over deem! bodies)")

# Install bundled grammars under the project share dir (/usr/share/logos/peg_gen_cpp
# with the package prefix). Reference/tooling data — the parser is generated
# into logosc at build time, so nothing reads this at runtime.
install(DIRECTORY grammars/ DESTINATION share/logos/peg_gen_cpp)
