# Copyright 2026 Victor Smirnov
# Logos project — https://github.com/victor-smirnov/logos
#
# hrpc_gen — HRPC IDL compiler.

# ── Step 1: generate the IDL parser from hrpc.peg ────────────────────────

set(HRPC_IDL_GRAMMAR      ${CMAKE_SOURCE_DIR}/tools/peg_gen/grammars/hrpc.peg)
set(HRPC_IDL_PARSER_HPP   ${CMAKE_CURRENT_BINARY_DIR}/hrpc_idl_parser.hpp)
set(HRPC_IDL_PARSER_CPP   ${CMAKE_CURRENT_BINARY_DIR}/hrpc_idl_parser.cpp)

add_custom_command(
    OUTPUT  ${HRPC_IDL_PARSER_HPP} ${HRPC_IDL_PARSER_CPP}
    COMMAND $<TARGET_FILE:peg_gen>
            ${HRPC_IDL_GRAMMAR}
            --out-dir ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS peg_gen ${HRPC_IDL_GRAMMAR}
    COMMENT "Generating hrpc_idl_parser from hrpc.peg"
)

# ── Step 2: hrpc_gen executable ───────────────────────────────────────────

add_executable(hrpc_gen
    src/main.cpp
    src/codegen.cpp
    ${HRPC_IDL_PARSER_CPP}
)
target_include_directories(hrpc_gen PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(hrpc_gen PRIVATE logos_hermes2 logos_core)

# ── Step 3: smoke test — parse only ──────────────────────────────────────

add_executable(hrpc_parse_test
    test/parse_test.cpp
    ${HRPC_IDL_PARSER_CPP}
)
target_include_directories(hrpc_parse_test PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(hrpc_parse_test PRIVATE logos_hermes2 logos_core)
