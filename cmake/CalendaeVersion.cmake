# Resolve the project version, in order of preference:
#
#   -DCALENDAE_VERSION=1.2.3   authoritative; CI passes the release tag here
#   `git describe`             e.g. v0.1.0-5-gdeadbee  (builds between tags)
#   0.0.0                      no git, no tags
#
# Produces two variables in the including scope:
#
#   CALENDAE_VERSION          strict MAJOR.MINOR.PATCH, for project(VERSION ...)
#   CALENDAE_VERSION_STRING   full human-facing string, for `calendae --version`
#
# This module is re-run on every CMake *configure*, not every build; re-run
# the configure step to pick up a new tag.

if(DEFINED CALENDAE_VERSION AND NOT "${CALENDAE_VERSION}" STREQUAL "")
    set(_ver "${CALENDAE_VERSION}")
else()
    set(_ver "")
    find_package(Git QUIET)
    if(Git_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty --match "v[0-9]*"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _ver
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
endif()

string(REGEX REPLACE "^v" "" _ver "${_ver}")
if("${_ver}" STREQUAL "")
    set(_ver "0.0.0-unknown")
endif()

set(CALENDAE_VERSION_STRING "${_ver}")
if(_ver MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(CALENDAE_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
else()
    # A bare commit hash from `git describe --always` before the first
    # version tag exists: keep project() happy, but make --version say so.
    set(CALENDAE_VERSION "0.0.0")
    if(NOT "${_ver}" STREQUAL "0.0.0-unknown")
        set(CALENDAE_VERSION_STRING "0.0.0-dev+${_ver}")
    endif()
endif()

message(STATUS "calendae: version ${CALENDAE_VERSION_STRING} (project ${CALENDAE_VERSION})")

unset(_ver)
