# Usage: cmake -DDIR=<folder> -DOUT=<file.zip> -P ZipDir.cmake
# Zips the folder's contents with paths relative to DIR (no "./" prefixes), in sorted order.
if(NOT EXISTS "${DIR}/index.html")
    message(FATAL_ERROR "ZipDir: ${DIR}/index.html not found. Did the UI build run?")
endif()

file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${DIR}" "${DIR}/*")
list(SORT files)
get_filename_component(out_dir "${OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${out_dir}")
file(REMOVE "${OUT}")

execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar cf "${OUT}" --format=zip -- ${files}
    WORKING_DIRECTORY "${DIR}"
    RESULT_VARIABLE rc)

if(NOT rc EQUAL 0)
    message(FATAL_ERROR "ZipDir: failed to create ${OUT}")
endif()
