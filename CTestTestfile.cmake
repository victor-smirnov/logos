# ⚠ THIS FILE EXISTS TO MAKE A MISTAKE LOUD. IT REGISTERS NO TESTS.
#
# `ctest` looks for CTestTestfile.cmake in the CURRENT directory. Run from the
# source root — which is where a shell most often already is — it found nothing
# and answered:
#
#     Test project /home/logos/devel/logos
#     No tests were found!!!
#     rc=1
#
# That is a FAILURE THAT READS LIKE REAL REDNESS. It is how a wrong directory
# gets mistaken for a broken tree, and both an agent and I minted a false "the
# gate is red" from it on 2026-08-27 before noticing the cwd.
#
# ⚠ THE POINT IS NOT THE REMINDER, IT IS THE IMPOSSIBILITY. The rule "always
# pass --test-dir" had been written into every agent prompt for two days and
# was followed 83% of the time; the 17% clustered on exactly the expensive
# commands. An instruction scales to most-of-the-time and stops. A file that
# makes the wrong invocation fail with its own name in the message does not
# depend on anyone remembering. Victor's prior art: he hard-blocked the
# directories cmake creates when invoked from the source root, so cmake errors
# instead of quietly configuring in-source.
#
# Correct invocations, unaffected by this file:
#     ctest --test-dir build -R <regex>
#     cd build && ctest -R <regex>
#     bash tests/logos/test-levels.sh L1     # from the BUILD dir
#
# ⚠ Kept out of .gitignore by an explicit negation for this path only; the
# generated CTestTestfile.cmake under build/ stays ignored.
message(FATAL_ERROR
  "ctest was invoked from the SOURCE ROOT, where no tests are registered.\n"
  "  Use:  ctest --test-dir build <args>\n"
  "  or:   cd build && ctest <args>\n"
  "This file registers no tests; it exists so that a wrong working directory "
  "says so, instead of answering 'No tests were found!!!' — which reads like a "
  "red tree and has been mistaken for one.")
