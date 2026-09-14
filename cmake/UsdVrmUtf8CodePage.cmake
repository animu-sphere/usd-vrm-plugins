# SPDX-License-Identifier: Apache-2.0
#
# usdvrm_use_utf8_code_page(<target>)
#
# Makes a Windows executable's process code page UTF-8, so the paths it is
# handed arrive in the encoding OpenUSD reads them in.
#
# OpenUSD treats every path string as UTF-8 on every platform. A Windows
# `main(int, char**)` receives its arguments in the process's ANSI code page
# instead -- CP932 on a Japanese host, CP1252 on a CI runner -- so a tool handed
# a non-ASCII path passed OpenUSD bytes it could not decode, and the libraries
# the tool calls opened the same string through the same code page. Measured
# 2026-09-15: `motion_retarget` could not find an avatar under a directory named
# in Japanese, and `motion_bvh_convert` read `é` as `e`.
#
# The manifest sets `activeCodePage` to UTF-8 (Windows 10 1903 and later), which
# makes `argv`, `getenv` and every narrow file API in the process UTF-8 at once,
# rather than converting at each call site. It is a property of the executable
# only: a plugin is loaded by a host whose code page is not ours, and reads its
# files through Ar for that reason.
#
# Every executable the workspace ships calls this: the four product tools and
# the three adapter recorders. `workspace_unicode_paths` runs all seven and is
# what fails if one stops.

function(usdvrm_use_utf8_code_page target)
    # The MSVC linker merges a `.manifest` source into the embedded manifest.
    if(MSVC)
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/utf8-code-page.manifest")
    endif()
endfunction()
