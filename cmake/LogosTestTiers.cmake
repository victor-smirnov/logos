# ── TEST TIERS: THE GATE DECLARES WHEN IT RUNS; THE SELECTOR ASKS ────────────
#
# THE DEFECT THIS RETIRES. `test-levels.sh` used to select the per-commit gates
# tier with a regex that LISTED NAMES:
#
#     -R '^logos_00_|^logos_07_ir_snapshot_|^logos_09_rtval_domain$|^logos_09_incr_eligibility_population$'
#
# A listed population answers "is this name in my list?", never "which tests
# want to run per commit?". Its failure mode is SILENCE: a gate added today
# matches no alternative, is in NEITHER tier, and nothing anywhere says so. That
# is this codebase's recurring gate-lies class — a population LISTED rather than
# DERIVED — and it had already been deferred three rounds when this landed.
#
# THE DERIVED FORM. Every test that is not corpus declares its own tier as a
# ctest LABEL, and the selector asks for the label:
#
#     tier_commit    cheap, runs on every commit (test-levels.sh L1-L3 gates tier)
#     tier_full      expensive, L4 only
#     tier_explicit  run BY NAME from a bespoke harness block, not through a
#                    tier selector — see below, there is exactly one today and
#                    the vocabulary exists so its label can state the truth
#                    rather than a convenient lie.
#
# THE LABEL IS ADDITIONAL, NOT A REPLACEMENT. A test keeps its existing
# "logos;pass;suite_semantic_core" labels and gains one tier label beside them.
#
# ⚠ THE CANARY IS THE POINT, NOT THE LABEL. Selecting on a label instead of a
# name is only half the fix: a gate that declares NOTHING would be silently
# dropped from every tier, which is the same defect with extra steps. So the
# rule is EXACTLY ONE tier label per non-corpus test, and violating it is a
# CONFIGURE-TIME FATAL ERROR.
#
# WHY CONFIGURE TIME RATHER THAN A TEST. A test that audits the labels would
# have to carry a tier label itself; if it lost one it would drop out of the
# commit tier and only L4 would notice — the defect being closed here, one level
# up. A FATAL_ERROR has no label, cannot be unselected by -R, -L,
# LOGOS_NO_GATES=1 or a chunk regex, and cannot be built past: `cmake --build`
# re-runs configure after a CMakeLists edit and propagates rc=1 (MEASURED). It
# fires on the edit, before any test runs, and names the offender.
#
# WHY TWO FUNCTIONS. `get_test_property()` reads labels only from the directory
# that defined the test — cross-directory `get_property(TEST ...)` is a hard
# CMake error (MEASURED) — so the label audit must run once PER DIRECTORY.
# That makes the set of audited directories a population in its own right, and a
# LISTED one would reintroduce the very defect. So it is derived too:
# `logos_audit_tier_coverage()` walks SUBDIRECTORIES from the root, asks each
# directory for its TESTS property (which IS readable cross-directory —
# MEASURED), and fails if any directory that defines tests never called
# `logos_require_tier_labels()`. Adding a whole new test directory and
# forgetting the audit is therefore also red.

set(LOGOS_TIER_VALUES tier_commit tier_full tier_explicit
    CACHE INTERNAL "the tier vocabulary; exactly one per non-corpus test")

# ⚠ LOAD-BEARING, NOT HYGIENE. The bottom of this file DEFERs the coverage audit
# onto the directory that includes it, and that must be the root — a second
# include from a subdirectory would schedule a second walk that fires at the end
# of THAT subdirectory, before its later siblings exist, reintroducing exactly
# the early-sampling defect the defer retires.
include_guard(GLOBAL)

# Call ONCE from any directory that defines tests, at any point in the file —
# the audit is DEFERred to the end of that directory's processing, so it sees
# every test added after the call as well as before.
function(logos_require_tier_labels)
    set_property(GLOBAL APPEND PROPERTY LOGOS_TIER_AUDITED_DIRS
                 "${CMAKE_CURRENT_SOURCE_DIR}")
    cmake_language(DEFER CALL _logos_tier_audit_dir)
endfunction()

function(_logos_tier_audit_dir)
    get_property(_tests DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY TESTS)
    set(_bad "")
    foreach(_t IN LISTS _tests)
        get_test_property(${_t} LABELS _lab)
        # An absent LABELS property reads back as NOTFOUND, which would survive
        # `IN_LIST` as a literal string. Normalise before asking.
        if(NOT _lab)
            set(_lab "")
        endif()
        # THE CORPUS IS EXEMPT, AND ITS EXEMPTION IS ALSO DERIVED. The 6800+
        # sampler-reachable corpus tests are selected by the per-level chunk
        # regexes, not by a tier, so a tier label on them would be noise. They
        # are identified by the `corpus` label their single registration funnel
        # stamps — NOT by a name prefix, which would be the listed population
        # wearing a canary's coat.
        if("corpus" IN_LIST _lab)
            continue()
        endif()
        set(_n 0)
        foreach(_v IN LISTS LOGOS_TIER_VALUES)
            if("${_v}" IN_LIST _lab)
                math(EXPR _n "${_n}+1")
            endif()
        endforeach()
        if(NOT _n EQUAL 1)
            # `;` is CMake's list separator, so a label list embedded raw would
            # be re-split into one line per label by the join below. Flatten to
            # a comma-joined string FIRST — the offender's actual labels are the
            # useful half of this message.
            string(REPLACE ";" ", " _lab_txt "${_lab}")
            list(APPEND _bad "${_t} — carries ${_n} tier labels, needs exactly 1 (has: ${_lab_txt})")
        endif()
    endforeach()
    if(_bad)
        string(REPLACE ";" "\n      " _bad_txt "${_bad}")
        string(REPLACE ";" ", " _vocab "${LOGOS_TIER_VALUES}")
        message(FATAL_ERROR
            "TIER CANARY — ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt\n"
            "Every non-corpus test must declare exactly ONE of: ${_vocab}\n"
            "A test that declares none is in NO tier: it would run nowhere while "
            "still looking registered, which is the silent drop this mechanism "
            "exists to make impossible. One that declares several makes the "
            "tiers overlap and double-counts its failure.\n"
            "  OFFENDING TESTS:\n      ${_bad_txt}\n"
            "See cmake/LogosTestTiers.cmake.")
    endif()
endfunction()

# Scheduled automatically at the bottom of this file — see there. Not called
# from any CMakeLists, on purpose: its correctness used to depend on WHERE the
# call sat, which is the same listed-population defect one level up again.
function(logos_audit_tier_coverage)
    get_property(_audited GLOBAL PROPERTY LOGOS_TIER_AUDITED_DIRS)
    set(_pending "${CMAKE_CURRENT_SOURCE_DIR}")
    set(_missing "")
    while(_pending)
        list(POP_FRONT _pending _d)
        get_property(_kids DIRECTORY "${_d}" PROPERTY SUBDIRECTORIES)
        list(APPEND _pending ${_kids})
        get_property(_t DIRECTORY "${_d}" PROPERTY TESTS)
        if(_t AND NOT "${_d}" IN_LIST _audited)
            list(APPEND _missing "${_d}")
        endif()
    endwhile()
    if(_missing)
        string(REPLACE ";" "\n    " _missing_txt "${_missing}")
        message(FATAL_ERROR
            "TIER CANARY — these directories register tests but never called "
            "logos_require_tier_labels(), so nothing checks that their tests "
            "declare a tier:\n    ${_missing_txt}\n"
            "See cmake/LogosTestTiers.cmake.")
    endif()
endfunction()

# ── THE COVERAGE AUDIT SCHEDULES ITSELF; THERE IS NO CALL SITE TO MISPLACE ────
#
# THE DEFECT THIS RETIRES, MEASURED. `logos_audit_tier_coverage()` used to be
# called explicitly from the root CMakeLists, under a comment reading "⚠ MUST
# FOLLOW EVERY add_subdirectory() ABOVE". That comment was the whole enforcement.
# The walk reads the SUBDIRECTORIES property, which only lists directories CMake
# has already descended into, so an `add_subdirectory()` placed BELOW the call
# is invisible to it. MEASURED on this tree: a new directory added one line after
# the call, registering a test with no tier label at all, configured rc=0 — and
# `ctest -N -L 'tier_commit|tier_full|tier_explicit'` matched it 0 times. A test
# registered in NO tier, silently, which is precisely the drop this whole
# mechanism exists to make impossible. The canary's own population was derived;
# the MOMENT it was sampled was still listed, by a comment.
#
# THE DERIVED FORM. Defer the call to the end of the root directory's
# processing. Every `add_subdirectory()` anywhere in the tree completes inside
# that processing, whatever order they appear in, so the walk cannot run early —
# there is no ordering left for a future edit to get wrong, and no comment left
# to obey. MEASURED: with this, the same late-added directory fails the
# configure with rc=1 and names the directory.
#
# ⚠ THE DEFER BINDS THIS FILE'S INCLUDER, which is the root CMakeLists (the only
# include). Deferred calls fire in registration order; this one registers before
# the root's own `logos_require_tier_labels()` and both still run, because every
# SUBDIRECTORY's per-directory audit fired at the end of that subdirectory, long
# before either.
cmake_language(DEFER CALL logos_audit_tier_coverage)
