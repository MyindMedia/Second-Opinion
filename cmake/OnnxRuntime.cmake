# ONNX Runtime (CPU execution provider) for Basic Pitch note transcription (P1-08, ADR 0009).
#
# Provides:
#   so::onnxruntime            imported shared library (headers + import lib)
#   SO_ORT_RUNTIME_FILES       runtime files that must ship next to the plugin binary
#   so_bundle_onnxruntime(<target> <dest-dir>)  post-build copy of those files
#
# Interim: the official prebuilt release (pinned URL + SHA-256), linked dynamically and copied
# into every plugin bundle. The minimal static reduced-operator build that the PRD asks for is
# planned in ADR 0009 and will replace this behind the same target name.
#
# Offline builds: pass -DSO_ORT_ROOT=/path/to/extracted/onnxruntime-<os>-<ver> (the folder that
# holds include/ and lib/), or -DSO_ORT_ARCHIVE=/path/to/the/release/archive.
#
# The archive is downloaded once into SO_ORT_DOWNLOAD_DIR (default: <CPM_SOURCE_CACHE>/onnxruntime
# when a CPM source cache is set, so Debug and Release presets and CI caches share it).
#
# Opt-in minimal static build (ADR 0009, Q20): -DSO_ORT_MINIMAL=ON -DSO_ORT_MINIMAL_ROOT=<prefix>
# where <prefix> was produced by cmake/ort_minimal/build_ort_minimal.py. Nothing is shipped next
# to the plugin binary in that mode; ORT is linked statically with its symbols hidden.

option(SO_ENABLE_ONNX "Build Basic Pitch transcription with ONNX Runtime" ON)

if(NOT SO_ENABLE_ONNX)
    return()
endif()

set(SO_ORT_VERSION "1.21.0")
set(SO_ORT_ROOT "" CACHE PATH "Extracted ONNX Runtime release (skips the download)")
set(SO_ORT_ARCHIVE "" CACHE FILEPATH "Local copy of the ONNX Runtime release archive (skips the download)")
option(SO_ORT_MINIMAL "Link a minimal, static, reduced-operator ONNX Runtime (ADR 0009)" OFF)
set(SO_ORT_MINIMAL_ROOT "" CACHE PATH "Prefix written by cmake/ort_minimal/build_ort_minimal.py")

# ONNX Runtime 1.20+ release binaries for macOS are built with a 13.3 minimum (LC_BUILD_VERSION
# minos 13.3 in both slices of the 1.21.0 universal2 dylib). Older systems may fail to load it.
if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "13.3")
    message(WARNING "CMAKE_OSX_DEPLOYMENT_TARGET is ${CMAKE_OSX_DEPLOYMENT_TARGET} but ONNX Runtime "
                    "${SO_ORT_VERSION} for macOS requires 13.3. The plugin links it, so it may not load on "
                    "macOS 12 to 13.2. Decide in Q9 (raise the minimum, or pin an older ORT). See ADR 0009.")
endif()

# ---------------------------------------------------------------------------------------------
# Minimal static build (opt-in)
# ---------------------------------------------------------------------------------------------
if(SO_ORT_MINIMAL)
    if(NOT EXISTS "${SO_ORT_MINIMAL_ROOT}/include/onnxruntime_cxx_api.h")
        message(FATAL_ERROR "SO_ORT_MINIMAL=ON needs SO_ORT_MINIMAL_ROOT=<prefix> with include/ and lib/ "
                            "(run cmake/ort_minimal/build_ort_minimal.py first). Got '${SO_ORT_MINIMAL_ROOT}'.")
    endif()
    if(WIN32)
        set(_ort_static "${SO_ORT_MINIMAL_ROOT}/lib/onnxruntime_minimal.lib")
    else()
        set(_ort_static "${SO_ORT_MINIMAL_ROOT}/lib/libonnxruntime_minimal.a")
    endif()
    if(NOT EXISTS "${_ort_static}")
        message(FATAL_ERROR "Minimal ONNX Runtime archive not found: ${_ort_static}")
    endif()

    find_package(Threads REQUIRED)
    add_library(so::onnxruntime INTERFACE IMPORTED GLOBAL)
    set_target_properties(so::onnxruntime PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${SO_ORT_MINIMAL_ROOT}/include"
        INTERFACE_COMPILE_DEFINITIONS "SO_ORT_STATIC=1")
    if(APPLE)
        # -load_hidden: every symbol pulled from the archive becomes hidden in the plugin binary, so
        # two plugins with their own static ORT can never interpose each other.
        target_link_libraries(so::onnxruntime INTERFACE "-Wl,-load_hidden,${_ort_static}"
                              "-framework Foundation" Threads::Threads)
    elseif(WIN32)
        # Static .lib symbols are never exported from the plugin DLL (ORT exports via a .def file
        # only in its own shared build).
        target_link_libraries(so::onnxruntime INTERFACE "${_ort_static}" advapi32 dbghelp shlwapi)
    else()
        target_link_libraries(so::onnxruntime INTERFACE "${_ort_static}" Threads::Threads ${CMAKE_DL_LIBS})
        target_link_options(so::onnxruntime INTERFACE "LINKER:--exclude-libs,ALL")
    endif()

    set(SO_ORT_RUNTIME_FILES "")
    set(SO_ORT_LICENSE_FILES "${SO_ORT_MINIMAL_ROOT}/LICENSE" "${SO_ORT_MINIMAL_ROOT}/ThirdPartyNotices.txt")
    # A minimal build only loads ORT-format models. The runner reads the file into memory and ORT
    # detects the format from the bytes, so the converted model ships under the existing name
    # (SessionEngine looks for basic_pitch/nmp.onnx) until the lookup learns nmp.ort.
    set(SO_ORT_MINIMAL_MODEL "${SO_ORT_MINIMAL_ROOT}/model/nmp.ort")
    message(STATUS "ONNX Runtime ${SO_ORT_VERSION} (minimal static): ${SO_ORT_MINIMAL_ROOT}")
endif()

if(NOT SO_ORT_MINIMAL)

# Hashes computed from the GitHub release assets on 2026-10-26 (ADR 0009).
if(APPLE)
    set(_ort_pkg "onnxruntime-osx-universal2-${SO_ORT_VERSION}")
    set(_ort_ext "tgz")
    set(_ort_sha256 "3c3cfc71e538e592192c14d9bad88ec5d5d8d5d698ebc2c6b3119be8c90b4670")
elseif(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|arm")
    set(_ort_pkg "onnxruntime-win-x64-${SO_ORT_VERSION}")
    set(_ort_ext "zip")
    set(_ort_sha256 "5c07bb2805cd666dda75fa9bfa60e75f2f90d478b952298dd9d55c00740d81bf")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(_ort_pkg "onnxruntime-linux-x64-${SO_ORT_VERSION}")
    set(_ort_ext "tgz")
    set(_ort_sha256 "7485c7e7aac6501b27e353dcbe068e45c61ab51fbaf598d13970dfae669d20bf")
else()
    message(WARNING "No pinned ONNX Runtime build for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}; "
                    "SO_ENABLE_ONNX turned OFF.")
    set(SO_ENABLE_ONNX OFF CACHE BOOL "" FORCE)
    return()
endif()

if(CPM_SOURCE_CACHE)
    set(_ort_dl_default "${CPM_SOURCE_CACHE}/onnxruntime")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
    set(_ort_dl_default "$ENV{CPM_SOURCE_CACHE}/onnxruntime")
else()
    set(_ort_dl_default "${CMAKE_BINARY_DIR}/_deps/onnxruntime")
endif()
set(SO_ORT_DOWNLOAD_DIR "${_ort_dl_default}" CACHE PATH "Where the ONNX Runtime archive is downloaded and extracted")

if(SO_ORT_ROOT)
    set(_ort_root "${SO_ORT_ROOT}")
else()
    set(_ort_dl_dir "${SO_ORT_DOWNLOAD_DIR}")
    set(_ort_root "${_ort_dl_dir}/${_ort_pkg}")
    if(NOT EXISTS "${_ort_root}/include/onnxruntime_cxx_api.h")
        if(SO_ORT_ARCHIVE)
            set(_ort_archive "${SO_ORT_ARCHIVE}")
            file(SHA256 "${_ort_archive}" _ort_actual)
            if(NOT _ort_actual STREQUAL _ort_sha256)
                message(FATAL_ERROR "SO_ORT_ARCHIVE hash mismatch: ${_ort_actual} (expected ${_ort_sha256})")
            endif()
        else()
            set(_ort_archive "${_ort_dl_dir}/${_ort_pkg}.${_ort_ext}")
            set(_ort_url "https://github.com/microsoft/onnxruntime/releases/download/v${SO_ORT_VERSION}/${_ort_pkg}.${_ort_ext}")
            message(STATUS "Downloading ${_ort_url}")
            file(DOWNLOAD "${_ort_url}" "${_ort_archive}"
                EXPECTED_HASH SHA256=${_ort_sha256}
                TLS_VERIFY ON
                STATUS _ort_status)
            list(GET _ort_status 0 _ort_code)
            if(NOT _ort_code EQUAL 0)
                list(GET _ort_status 1 _ort_msg)
                message(FATAL_ERROR "ONNX Runtime download failed (${_ort_msg}). Configure with "
                                    "-DSO_ORT_ARCHIVE=<archive> or -DSO_ORT_ROOT=<dir>, or -DSO_ENABLE_ONNX=OFF.")
            endif()
        endif()
        # Extract next to the final folder, then rename, so an interrupted or parallel configure
        # never leaves a half-extracted tree that looks complete.
        string(RANDOM LENGTH 8 _ort_rand)
        set(_ort_tmp "${_ort_dl_dir}/.extract-${_ort_rand}")
        file(ARCHIVE_EXTRACT INPUT "${_ort_archive}" DESTINATION "${_ort_tmp}")
        if(NOT EXISTS "${_ort_root}/include/onnxruntime_cxx_api.h")
            file(REMOVE_RECURSE "${_ort_root}")
            file(RENAME "${_ort_tmp}/${_ort_pkg}" "${_ort_root}")
        endif()
        file(REMOVE_RECURSE "${_ort_tmp}")
    endif()
endif()

if(NOT EXISTS "${_ort_root}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "ONNX Runtime headers not found under ${_ort_root}")
endif()

add_library(so::onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(so::onnxruntime PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_ort_root}/include")

if(APPLE)
    set(_ort_lib "${_ort_root}/lib/libonnxruntime.${SO_ORT_VERSION}.dylib")
    # The release dylib is universal2 but only its arm64 slice carries a (linker) signature; the
    # x86_64 slice is unsigned. Xcode then refuses to sign a bundle that nests it ("code object is
    # not signed at all, in subcomponent"). Ad-hoc sign a private copy once; release signing
    # re-signs it with the Developer ID (packaging/macos).
    find_program(SO_CODESIGN codesign)
    if(SO_CODESIGN)
        set(_ort_signed_dir "${CMAKE_BINARY_DIR}/_deps/onnxruntime-adhoc")
        set(_ort_signed "${_ort_signed_dir}/libonnxruntime.${SO_ORT_VERSION}.dylib")
        set(_ort_stamp "${_ort_signed_dir}/adhoc.stamp")
        set(_ort_stamp_text "${_ort_lib} ${_ort_sha256}")
        set(_ort_stamp_old "")
        if(EXISTS "${_ort_stamp}")
            file(READ "${_ort_stamp}" _ort_stamp_old)
        endif()
        if(NOT EXISTS "${_ort_signed}" OR NOT _ort_stamp_old STREQUAL _ort_stamp_text)
            file(MAKE_DIRECTORY "${_ort_signed_dir}")
            file(COPY_FILE "${_ort_lib}" "${_ort_signed}")
            execute_process(COMMAND "${SO_CODESIGN}" --force --sign - "${_ort_signed}"
                            RESULT_VARIABLE _ort_cs_rc ERROR_VARIABLE _ort_cs_err)
            if(NOT _ort_cs_rc EQUAL 0)
                message(FATAL_ERROR "codesign of ${_ort_signed} failed: ${_ort_cs_err}")
            endif()
            file(WRITE "${_ort_stamp}" "${_ort_stamp_text}")
        endif()
        set(_ort_lib "${_ort_signed}")
    endif()
    set_target_properties(so::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${_ort_lib}"
        IMPORTED_SONAME "@rpath/libonnxruntime.${SO_ORT_VERSION}.dylib")
    set(SO_ORT_RUNTIME_FILES "${_ort_lib}")
elseif(WIN32)
    set_target_properties(so::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${_ort_root}/lib/onnxruntime.dll"
        IMPORTED_IMPLIB "${_ort_root}/lib/onnxruntime.lib")
    set(SO_ORT_RUNTIME_FILES "${_ort_root}/lib/onnxruntime.dll")
else()
    # Ship the file under its SONAME so the dynamic linker finds it through $ORIGIN.
    set(_ort_lib "${_ort_root}/lib/libonnxruntime.so.${SO_ORT_VERSION}")
    set_target_properties(so::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${_ort_lib}"
        IMPORTED_SONAME "libonnxruntime.so.1")
    set(SO_ORT_RUNTIME_FILES "${_ort_lib}")
endif()

set(SO_ORT_LICENSE_FILES "${_ort_root}/LICENSE" "${_ort_root}/ThirdPartyNotices.txt")
message(STATUS "ONNX Runtime ${SO_ORT_VERSION}: ${_ort_root}")
endif()   # NOT SO_ORT_MINIMAL

# Copies the runtime library into <dest-dir> after <target> builds. On Linux the file is renamed
# to its SONAME (libonnxruntime.so.1), which is what the loader looks for.
function(so_bundle_onnxruntime target dest_dir)
    foreach(_f IN LISTS SO_ORT_RUNTIME_FILES)
        get_filename_component(_name "${_f}" NAME)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(_name "libonnxruntime.so.1")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${dest_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_f}" "${dest_dir}/${_name}"
            VERBATIM)
    endforeach()
endfunction()

# Puts everything Basic Pitch needs at runtime into each built format of a juce_add_plugin target:
#   macOS bundles (VST3, AU, Standalone .app): Contents/Frameworks/libonnxruntime.<ver>.dylib,
#     rpath @loader_path/../Frameworks, model under Contents/Resources/basic_pitch. Added as bundle
#     sources, so they are in place before JUCE's copy-after-build and code-signing steps.
#   Windows and Linux: runtime library next to the plugin binary (VST3 Contents/<arch>/ or the
#     Standalone folder), rpath $ORIGIN on Linux; model under Resources/basic_pitch (VST3:
#     Contents/Resources/basic_pitch). Post-build copies, so leave SO_COPY_PLUGIN OFF there.
function(so_bundle_basic_pitch shared_target)
    set(_model_files ${ARGN})
    foreach(_fmt IN ITEMS VST3 AU Standalone)
        set(_t ${shared_target}_${_fmt})
        if(NOT TARGET ${_t})
            continue()
        endif()

        if(APPLE)
            target_sources(${_t} PRIVATE ${SO_ORT_RUNTIME_FILES} ${_model_files} ${SO_ORT_LICENSE_FILES})
            if(SO_ORT_RUNTIME_FILES)
                set_source_files_properties(${SO_ORT_RUNTIME_FILES} TARGET_DIRECTORY ${_t}
                    PROPERTIES MACOSX_PACKAGE_LOCATION Frameworks)
            endif()
            set_source_files_properties(${_model_files} TARGET_DIRECTORY ${_t}
                PROPERTIES MACOSX_PACKAGE_LOCATION Resources/basic_pitch)
            set_source_files_properties(${SO_ORT_LICENSE_FILES} TARGET_DIRECTORY ${_t}
                PROPERTIES MACOSX_PACKAGE_LOCATION Resources/licenses/onnxruntime)
            set_target_properties(${_t} PROPERTIES
                BUILD_WITH_INSTALL_RPATH ON
                INSTALL_RPATH "@loader_path/../Frameworks")
        else()
            set(_bin "$<TARGET_FILE_DIR:${_t}>")
            if(_fmt STREQUAL "VST3")
                set(_res "${_bin}/../Resources")
            else()
                set(_res "${_bin}/Resources")
            endif()
            so_bundle_onnxruntime(${_t} "${_bin}")
            add_custom_command(TARGET ${_t} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_res}/basic_pitch" "${_res}/licenses/onnxruntime"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_model_files} "${_res}/basic_pitch/"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SO_ORT_LICENSE_FILES} "${_res}/licenses/onnxruntime/"
                VERBATIM)
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                set_target_properties(${_t} PROPERTIES
                    BUILD_WITH_INSTALL_RPATH ON
                    INSTALL_RPATH "$ORIGIN")
            endif()
        endif()
    endforeach()
endfunction()
