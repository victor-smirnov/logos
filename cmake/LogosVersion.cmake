# The build-metadata part of the version: what makes a snapshot build's
# LOGOS_VERSION_FULL name its source. Used at configure time (messages,
# CPack) and, in script mode, at EVERY build to regenerate version.cpp, so
# `--version` and the archives' `@abi` stamp follow the commit and the working
# tree without a reconfigure:
#
#   cmake -DSRC_DIR=... -DBASE_FULL=... -DSLOT=... -DTEMPLATE=... -DOUT=...
#         -P LogosVersion.cmake
#
# version.cpp is rewritten only when its text changes, so a build that changes
# nothing recompiles nothing.

# The version names the compiler's INPUTS, not the whole tree: a commit or an
# edit that touches only tests, docs or scripts must not change the version,
# or it would relink logosc and rebuild the stdlib (whose archives carry the
# version as their `@abi` stamp). The commit named is the last one that changed
# an input; the dirty hash covers only uncommitted changes to inputs. Inputs
# are everything EXCEPT the paths below: a path wrongly listed here can leave
# the version stale, so the list names only what is certainly not built into
# logosc or the stdlib.
set(LOGOS_VERSION_NON_INPUTS
    ":!tests" ":!docs" ":!examples" ":!scripts" ":!bench" ":!agent" ":!sandbox"
    ":!docker" ":!packaging" ":!dist" ":!abi" ":!*.md")

# Sets <out_disc> (branch-g<sha>[-dirty], or "snapshot" with no git) and
# <out_full_disc> (the same plus the content hash of an uncommitted change, or
# a timestamp with no git).
function(logos_snapshot_disc src_dir out_disc out_full_disc)
    set(_disc "snapshot")
    set(_full "")
    find_package(Git QUIET)
    if(Git_FOUND AND IS_DIRECTORY ${src_dir}/.git)
        execute_process(COMMAND ${GIT_EXECUTABLE} log -1 --format=%h --abbrev=8 HEAD
                -- . ${LOGOS_VERSION_NON_INPUTS}
            WORKING_DIRECTORY ${src_dir} OUTPUT_VARIABLE _sha
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
            WORKING_DIRECTORY ${src_dir} OUTPUT_VARIABLE _branch
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no
                -- . ${LOGOS_VERSION_NON_INPUTS}
            WORKING_DIRECTORY ${src_dir} OUTPUT_VARIABLE _dirty
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(_sha)
            string(TOLOWER "${_branch}" _branch)
            string(REGEX REPLACE "[^a-z0-9]+" "-" _branch "${_branch}")  # slot/package-name safe
            string(REGEX REPLACE "^-+|-+$" "" _branch "${_branch}")
            if(NOT _branch OR _branch STREQUAL "head")
                set(_branch "detached")
            endif()
            set(_disc "${_branch}-g${_sha}")
            if(_dirty)
                set(_disc "${_disc}-dirty")
                # The CONTENT of the uncommitted change identifies a dirty build:
                # the same tree always gets the same version, a different edit a
                # different one.
                execute_process(COMMAND ${GIT_EXECUTABLE} diff HEAD
                        -- . ${LOGOS_VERSION_NON_INPUTS}
                    WORKING_DIRECTORY ${src_dir} OUTPUT_VARIABLE _diff ERROR_QUIET)
                string(SHA256 _h "${_diff}")
                string(SUBSTRING "${_h}" 0 12 _h)
                set(_full "${_disc}.${_h}")
            else()
                set(_full "${_disc}")   # a clean input commit identifies itself
            endif()
        endif()
    endif()
    if(NOT _full)
        # No git (a tarball, the Docker .deb): nothing identifies the source,
        # so a timestamp is the only distinguishing mark left.
        string(TIMESTAMP _ts "%Y%m%dT%H%M%SZ" UTC)
        set(_full "${_disc}.${_ts}")
    endif()
    set(${out_disc} "${_disc}" PARENT_SCOPE)
    set(${out_full_disc} "${_full}" PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE AND OUT)
    set(LOGOS_VERSION_FULL "${BASE_FULL}")
    if(NOT RELEASE)
        logos_snapshot_disc("${SRC_DIR}" _d _fd)
        set(LOGOS_VERSION_FULL "${BASE_FULL}+${_fd}")
    endif()
    set(LOGOS_VERSION_SLOT "${SLOT}")
    configure_file("${TEMPLATE}" "${OUT}.tmp" @ONLY)
    file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
    file(REMOVE "${OUT}.tmp")
endif()
