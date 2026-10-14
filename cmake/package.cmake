foreach(required IN ITEMS
        PLUGIN_FILE INI_FILE README_FILE MINHOOK_LICENSE_FILE OUTPUT_DIR ARCHIVE_FILE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing packaging argument: ${required}")
    endif()
endforeach()

foreach(input IN ITEMS PLUGIN_FILE INI_FILE README_FILE MINHOOK_LICENSE_FILE)
    if(NOT EXISTS "${${input}}")
        message(FATAL_ERROR "Packaging input does not exist: ${${input}}")
    endif()
endforeach()

set(include_loader FALSE)
if(DEFINED LOADER_FILE OR DEFINED LOADER_LICENSE_FILE)
    if(NOT DEFINED LOADER_FILE OR "${LOADER_FILE}" STREQUAL "" OR
       NOT DEFINED LOADER_LICENSE_FILE OR "${LOADER_LICENSE_FILE}" STREQUAL "")
        message(FATAL_ERROR "Loader packages require both loader and license")
    endif()
    foreach(input IN ITEMS LOADER_FILE LOADER_LICENSE_FILE)
        if(NOT EXISTS "${${input}}")
            message(FATAL_ERROR "Packaging input does not exist: ${${input}}")
        endif()
    endforeach()
    set(include_loader TRUE)
endif()

file(READ "${README_FILE}" readme_contents)
set(required_footer
    "For more game fixes, visit https://slop-blog.enkhayzomachines.net/fixes :)")
string(FIND "${readme_contents}" "Misc" misc_heading_offset)
string(FIND "${readme_contents}" "${required_footer}" footer_offset)
if(misc_heading_offset EQUAL -1 OR footer_offset EQUAL -1)
    message(FATAL_ERROR
        "Release README must retain the plain-text Misc section and fixes URL")
endif()

file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(COPY_FILE "${PLUGIN_FILE}" "${OUTPUT_DIR}/SOMA.FPSFix.asi" ONLY_IF_DIFFERENT)
file(COPY_FILE "${INI_FILE}" "${OUTPUT_DIR}/SOMA.FPSFix.ini" ONLY_IF_DIFFERENT)
file(COPY_FILE "${README_FILE}" "${OUTPUT_DIR}/README.txt" ONLY_IF_DIFFERENT)
file(COPY_FILE "${MINHOOK_LICENSE_FILE}" "${OUTPUT_DIR}/MINHOOK-LICENSE.txt"
     ONLY_IF_DIFFERENT)

set(package_files
    "SOMA.FPSFix.asi"
    "SOMA.FPSFix.ini"
    "README.txt"
    "MINHOOK-LICENSE.txt")
if(include_loader)
    file(COPY_FILE "${LOADER_FILE}" "${OUTPUT_DIR}/version.dll"
         ONLY_IF_DIFFERENT)
    file(COPY_FILE "${LOADER_LICENSE_FILE}"
         "${OUTPUT_DIR}/ULTIMATE-ASI-LOADER-LICENSE.txt" ONLY_IF_DIFFERENT)
    list(APPEND package_files
        "version.dll"
        "ULTIMATE-ASI-LOADER-LICENSE.txt")
endif()
set(checksums "")
foreach(file IN LISTS package_files)
    file(SHA256 "${OUTPUT_DIR}/${file}" hash)
    string(TOUPPER "${hash}" hash)
    string(APPEND checksums "${hash}  ${file}\n")
endforeach()
file(WRITE "${OUTPUT_DIR}/SHA256SUMS.txt" "${checksums}")
list(APPEND package_files "SHA256SUMS.txt")

get_filename_component(archive_dir "${ARCHIVE_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${archive_dir}")
file(REMOVE "${ARCHIVE_FILE}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${ARCHIVE_FILE}" --format=zip ${package_files}
    WORKING_DIRECTORY "${OUTPUT_DIR}"
    RESULT_VARIABLE archive_result
)
if(NOT archive_result EQUAL 0)
    message(FATAL_ERROR "Could not create release archive: ${ARCHIVE_FILE}")
endif()

