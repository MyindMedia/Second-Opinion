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

option(SO_ENABLE_ONNX "Build Basic Pitch transcription with ONNX Runtime" ON)

if(NOT SO_ENABLE_ONNX)
    return()
endif()

set(SO_ORT_VERSION "1.21.0")
set(SO_ORT_ROOT "" CACHE PATH "Extracted ONNX Runtime release (skips the download)")
set(SO_ORT_ARCHIVE "" CACHE FILEPATH "Local copy of the ONNX Runtime release archive (skips the download)")

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

if(SO_ORT_ROOT)
    set(_ort_root "${SO_ORT_ROOT}")
else()
    set(_ort_dl_dir "${CMAKE_BINARY_DIR}/_deps/onnxruntime")
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
        file(ARCHIVE_EXTRACT INPUT "${_ort_archive}" DESTINATION "${_ort_dl_dir}")
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
            set_source_files_properties(${SO_ORT_RUNTIME_FILES} TARGET_DIRECTORY ${_t}
                PROPERTIES MACOSX_PACKAGE_LOCATION Frameworks)
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
