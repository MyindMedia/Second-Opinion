# Builds the Vite/TypeScript UI in ui/ and embeds it as one zip in BinaryData.
#
#   npm found        → npm ci (when package-lock.json changes) + npm run build on every UI change
#   npm not found    → uses a prebuilt ui/dist if present (so the C++ build works without Node)
#
# The zip keeps file names stable for the ResourceProvider and avoids configure-time globbing of
# hashed asset names.

function(so_add_web_ui target)
    set(UI_DIR ${CMAKE_SOURCE_DIR}/ui)
    set(UI_DIST ${UI_DIR}/dist)
    set(UI_ZIP ${CMAKE_BINARY_DIR}/ui/ui.zip)

    find_program(SO_NPM NAMES npm npm.cmd)

    file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS
        ${UI_DIR}/src/*
        ${UI_DIR}/public/*)
    list(APPEND UI_SOURCES
        ${UI_DIR}/index.html
        ${UI_DIR}/package.json
        ${UI_DIR}/vite.config.ts
        ${UI_DIR}/tsconfig.json)

    if(SO_NPM)
        message(STATUS "Second Opinion UI: building with ${SO_NPM}")
        set(NPM_STAMP ${CMAKE_BINARY_DIR}/ui/npm-ci.stamp)

        # Xcode and IDEs often build with a minimal PATH (no Homebrew), and npm needs `node` on PATH.
        # Pin the folder npm was found in, as seen at configure time.
        set(NPM_RUN ${SO_NPM})
        if(NOT WIN32)
            get_filename_component(SO_NODE_DIR "${SO_NPM}" DIRECTORY)
            set(NPM_RUN ${CMAKE_COMMAND} -E env "PATH=${SO_NODE_DIR}:$ENV{PATH}" ${SO_NPM})
        endif()

        add_custom_command(
            OUTPUT ${NPM_STAMP}
            COMMAND ${NPM_RUN} ci --no-audit --no-fund
            COMMAND ${CMAKE_COMMAND} -E touch ${NPM_STAMP}
            WORKING_DIRECTORY ${UI_DIR}
            DEPENDS ${UI_DIR}/package-lock.json
            COMMENT "Installing UI dependencies (npm ci)"
            VERBATIM)

        add_custom_command(
            OUTPUT ${UI_ZIP}
            COMMAND ${NPM_RUN} run build
            COMMAND ${CMAKE_COMMAND} -DDIR=${UI_DIST} -DOUT=${UI_ZIP} -P ${CMAKE_SOURCE_DIR}/cmake/ZipDir.cmake
            WORKING_DIRECTORY ${UI_DIR}
            DEPENDS ${NPM_STAMP} ${UI_SOURCES}
            COMMENT "Building the WebView UI (vite build)"
            VERBATIM)
    elseif(EXISTS ${UI_DIST}/index.html)
        message(STATUS "Second Opinion UI: npm not found, embedding prebuilt ui/dist")
        file(GLOB_RECURSE UI_DIST_FILES CONFIGURE_DEPENDS ${UI_DIST}/*)

        add_custom_command(
            OUTPUT ${UI_ZIP}
            COMMAND ${CMAKE_COMMAND} -DDIR=${UI_DIST} -DOUT=${UI_ZIP} -P ${CMAKE_SOURCE_DIR}/cmake/ZipDir.cmake
            DEPENDS ${UI_DIST_FILES}
            COMMENT "Packing prebuilt ui/dist"
            VERBATIM)
    else()
        message(FATAL_ERROR "Second Opinion UI: install Node.js 20+ (npm) or provide a prebuilt ui/dist")
    endif()

    juce_add_binary_data(${target}
        NAMESPACE SecondOpinionUIData
        HEADER_NAME SecondOpinionUIData.h
        SOURCES ${UI_ZIP})
endfunction()
