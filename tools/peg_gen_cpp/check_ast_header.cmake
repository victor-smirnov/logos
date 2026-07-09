# Fails if include/logos/compiler/ast.hpp is not what the grammar generates.
#
# The header used to be hand-maintained beside logos.peg's %fields/%nodes — two
# declarations of the same integers, cross-checked by nobody. A missing entry is
# a compile error (safe); a MISMATCHED one is a silent runtime bug, and one had
# already happened. Now the grammar is the only declaration and this test is the
# thing that keeps it so.
#
#   cmake -DPEG=<peg_gen_cpp> -DGRAMMAR=<logos.peg> -DHEADER=<ast.hpp>
#         -DWORK=<scratch dir> -P check_ast_header.cmake

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

execute_process(
    COMMAND "${PEG}" "${GRAMMAR}" --out-dir "${WORK}" --ast-header "${WORK}/ast.hpp"
    RESULT_VARIABLE rc
    OUTPUT_QUIET)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "peg_gen_cpp failed on ${GRAMMAR}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${HEADER}" "${WORK}/ast.hpp"
    RESULT_VARIABLE diff_rc)

if(NOT diff_rc EQUAL 0)
    message(FATAL_ERROR
        "${HEADER} is STALE.\n"
        "The AST constants are generated from the grammar's %fields/%nodes.\n"
        "Regenerate and commit:\n"
        "  cmake --build <build> --target peg_ast_regen")
endif()

message(STATUS "ast.hpp is up to date with the grammar")
