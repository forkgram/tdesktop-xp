# Apply the XP adaptations to the submodules at configure time.
#
# The submodules are pinned at the commits upstream itself pins; everything this port
# changes inside them lives in this directory, one patch per submodule. Included from the
# top of CMakeLists.txt, before any include(cmake/...), because the cmake helper submodule
# is patched too. Re-running configure is safe: an already applied patch is skipped.

option(XP_SKIP_SUBMODULE_PATCHES "Do not apply the XP submodule patches" OFF)

set(xp_patch_list
    "Telegram/codegen"                  "codegen.patch"
    "Telegram/lib_base"                 "lib_base.patch"
    "Telegram/lib_spellcheck"           "lib_spellcheck.patch"
    "Telegram/lib_ui"                   "lib_ui.patch"
    "Telegram/ThirdParty/libtgvoip"     "libtgvoip.patch"
)

if (XP_SKIP_SUBMODULE_PATCHES)
    message(STATUS "XP patches: skipped on request")
    return()
endif()

find_program(XP_GIT_EXECUTABLE NAMES git git.exe)
if (NOT XP_GIT_EXECUTABLE)
    message(FATAL_ERROR "XP patches: git not found, cannot adapt the submodules.")
endif()

set(xp_applied 0)
set(xp_already 0)
list(LENGTH xp_patch_list xp_patch_count)
if (xp_patch_count GREATER 0)
    math(EXPR xp_last "${xp_patch_count} - 1")
    foreach (i RANGE 0 ${xp_last} 2)
        list(GET xp_patch_list ${i} xp_dir)
        math(EXPR j "${i} + 1")
        list(GET xp_patch_list ${j} xp_file)
        set(xp_full "${CMAKE_CURRENT_SOURCE_DIR}/${xp_dir}")
        set(xp_patch "${CMAKE_CURRENT_SOURCE_DIR}/patches/${xp_file}")
        if (NOT EXISTS "${xp_full}/.git")
            message(FATAL_ERROR
                "XP patches: submodule '${xp_dir}' is not checked out.\n"
                "Run: git submodule update --init --recursive")
        endif()
        execute_process(
            COMMAND "${XP_GIT_EXECUTABLE}" -C "${xp_full}" apply --reverse --check --binary "${xp_patch}"
            RESULT_VARIABLE xp_reverse_ok OUTPUT_QUIET ERROR_QUIET)
        if (xp_reverse_ok EQUAL 0)
            math(EXPR xp_already "${xp_already} + 1")
            continue()
        endif()
        execute_process(
            COMMAND "${XP_GIT_EXECUTABLE}" -C "${xp_full}" apply --binary --whitespace=nowarn "${xp_patch}"
            RESULT_VARIABLE xp_apply ERROR_VARIABLE xp_apply_error)
        if (NOT xp_apply EQUAL 0)
            # Fallback for clones whose line endings were rewritten by the client's
            # core.autocrlf: --ignore-whitespace tolerates the stray CR in the context.
            execute_process(
                COMMAND "${XP_GIT_EXECUTABLE}" -C "${xp_full}" apply --binary --whitespace=nowarn --ignore-whitespace "${xp_patch}"
                RESULT_VARIABLE xp_apply ERROR_VARIABLE xp_apply_error)
        endif()
        if (NOT xp_apply EQUAL 0)
            message(FATAL_ERROR "XP patches: failed to apply ${xp_file}:\n${xp_apply_error}")
        endif()
        message(STATUS "XP patches: applied ${xp_file} to ${xp_dir}")
        math(EXPR xp_applied "${xp_applied} + 1")
    endforeach()
endif()
message(STATUS "XP patches: ${xp_applied} applied, ${xp_already} already in place")
