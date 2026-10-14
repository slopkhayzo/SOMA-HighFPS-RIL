cmake_minimum_required(VERSION 3.24)

project(
    SOMAFpsFix
    VERSION 1.0.0
    DESCRIPTION "High-frame-rate presentation fix for SOMA"
    LANGUAGES C CXX
)

include(CTest)
include(FetchContent)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "SOMA.FPSFix supports 64-bit Windows/MSVC only")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Configure an x64 build environment; Soma.exe is a 64-bit process")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL
    "Do not update pinned dependencies after their first download" FORCE)

# MinHook v1.3.4. Fetching is confined to configure time; release packages
# record the bundled license separately.
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(minhook)
set_property(TARGET minhook PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

add_library(soma_portable STATIC
    src/agent_mapping.cpp
    src/camera_interpolation.cpp
    src/engine_mapping.cpp
    src/frame_cadence.cpp
    src/gui_mouse_mapping.cpp
    src/hands_interpolation.cpp
    src/hands_mapping.cpp
    src/mouse_look_preview.cpp
    src/pose_interpolation.cpp
    src/presentation_clock.cpp
    src/scene_mapping.cpp
    src/signature_scan.cpp
    src/world_mapping.cpp
    src/world_interpolation.cpp
)
target_include_directories(soma_portable PUBLIC src)
target_compile_features(soma_portable PUBLIC cxx_std_20)
target_compile_options(soma_portable PRIVATE
    /W4
    /WX
    /permissive-
    /utf-8
    /Zc:preprocessor
)
set_property(TARGET soma_portable PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# MODULE is appropriate for a LoadLibrary-loaded ASI and avoids producing an
# unnecessary consumer import library.
add_library(SomaFpsFix MODULE
    src/bootstrap.cpp
    src/config.cpp
    src/dllmain.cpp
    src/logger.cpp
    src/runtime.cpp
    src/target_mapping.cpp
)
target_include_directories(SomaFpsFix PRIVATE src)
target_compile_features(SomaFpsFix PRIVATE cxx_std_20)
target_compile_definitions(SomaFpsFix PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)
target_compile_options(SomaFpsFix PRIVATE
    /W4
    /WX
    /permissive-
    /utf-8
    /Zc:preprocessor
)
target_link_options(SomaFpsFix PRIVATE
    /DYNAMICBASE
    /NXCOMPAT
    /GUARD:CF
    /INCREMENTAL:NO
)
target_link_libraries(SomaFpsFix PRIVATE soma_portable minhook)
set_property(TARGET SomaFpsFix PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
set_target_properties(SomaFpsFix PROPERTIES
    OUTPUT_NAME "SOMA.FPSFix"
    PREFIX ""
    SUFFIX ".asi"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_command(TARGET SomaFpsFix POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config/SOMA.FPSFix.ini"
            "$<TARGET_FILE_DIR:SomaFpsFix>/SOMA.FPSFix.ini"
    VERBATIM
)

install(TARGETS SomaFpsFix LIBRARY DESTINATION . RUNTIME DESTINATION .)
install(FILES config/SOMA.FPSFix.ini DESTINATION .)

set(SOMA_PACKAGE_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/package.cmake")
add_custom_target(package_plugin_only
    COMMAND "${CMAKE_COMMAND}"
            -DPLUGIN_FILE=$<TARGET_FILE:SomaFpsFix>
            -DINI_FILE=${CMAKE_SOURCE_DIR}/config/SOMA.FPSFix.ini
            -DREADME_FILE=${CMAKE_SOURCE_DIR}/packaging/README.txt
            -DMINHOOK_LICENSE_FILE=${minhook_SOURCE_DIR}/LICENSE.txt
            -DOUTPUT_DIR=${CMAKE_SOURCE_DIR}/dist/release/SOMA.FPSFix-${PROJECT_VERSION}-plugin-only
            -DARCHIVE_FILE=${CMAKE_SOURCE_DIR}/dist/SOMA.FPSFix-${PROJECT_VERSION}-plugin-only.zip
            -P "${SOMA_PACKAGE_SCRIPT}"
    DEPENDS SomaFpsFix
    VERBATIM
)

set(ULTIMATE_ASI_LOADER_DLL "" CACHE FILEPATH
    "Optional pinned x64 Ultimate ASI Loader DLL used for a loader bundle")
set(ULTIMATE_ASI_LOADER_LICENSE "" CACHE FILEPATH
    "License file corresponding to ULTIMATE_ASI_LOADER_DLL")
if((ULTIMATE_ASI_LOADER_DLL AND NOT ULTIMATE_ASI_LOADER_LICENSE) OR
   (ULTIMATE_ASI_LOADER_LICENSE AND NOT ULTIMATE_ASI_LOADER_DLL))
    message(FATAL_ERROR
        "Set both ULTIMATE_ASI_LOADER_DLL and ULTIMATE_ASI_LOADER_LICENSE")
endif()
if(ULTIMATE_ASI_LOADER_DLL AND ULTIMATE_ASI_LOADER_LICENSE)
    add_custom_target(package_with_loader
        COMMAND "${CMAKE_COMMAND}"
                -DPLUGIN_FILE=$<TARGET_FILE:SomaFpsFix>
                -DINI_FILE=${CMAKE_SOURCE_DIR}/config/SOMA.FPSFix.ini
                -DREADME_FILE=${CMAKE_SOURCE_DIR}/packaging/README-with-loader.txt
                -DMINHOOK_LICENSE_FILE=${minhook_SOURCE_DIR}/LICENSE.txt
                -DLOADER_FILE=${ULTIMATE_ASI_LOADER_DLL}
                -DLOADER_LICENSE_FILE=${ULTIMATE_ASI_LOADER_LICENSE}
                -DOUTPUT_DIR=${CMAKE_SOURCE_DIR}/dist/release/SOMA.FPSFix-${PROJECT_VERSION}-with-Ultimate-ASI-Loader
                -DARCHIVE_FILE=${CMAKE_SOURCE_DIR}/dist/SOMA.FPSFix-${PROJECT_VERSION}-with-Ultimate-ASI-Loader.zip
                -P "${SOMA_PACKAGE_SCRIPT}"
        DEPENDS SomaFpsFix
        VERBATIM
    )
endif()

if(BUILD_TESTING)
    add_executable(presentation_clock_tests tests/presentation_clock_tests.cpp)
    target_link_libraries(presentation_clock_tests PRIVATE soma_portable)

    add_executable(agent_mapping_tests tests/agent_mapping_tests.cpp)
    target_link_libraries(agent_mapping_tests PRIVATE soma_portable)

    add_executable(camera_interpolation_tests
        tests/camera_interpolation_tests.cpp)
    target_link_libraries(camera_interpolation_tests PRIVATE soma_portable)

    add_executable(frame_cadence_tests tests/frame_cadence_tests.cpp)
    target_link_libraries(frame_cadence_tests PRIVATE soma_portable)

    add_executable(mouse_look_preview_tests
        tests/mouse_look_preview_tests.cpp)
    target_link_libraries(mouse_look_preview_tests PRIVATE soma_portable)

    add_executable(hands_mapping_tests tests/hands_mapping_tests.cpp)
    target_link_libraries(hands_mapping_tests PRIVATE soma_portable)

    add_executable(gui_mouse_mapping_tests
        tests/gui_mouse_mapping_tests.cpp)
    target_link_libraries(gui_mouse_mapping_tests PRIVATE soma_portable)

    add_executable(hands_interpolation_tests
        tests/hands_interpolation_tests.cpp)
    target_link_libraries(hands_interpolation_tests PRIVATE soma_portable)

    add_executable(pose_interpolation_tests
        tests/pose_interpolation_tests.cpp)
    target_link_libraries(pose_interpolation_tests PRIVATE soma_portable)

    add_executable(engine_mapping_tests tests/engine_mapping_tests.cpp)
    target_link_libraries(engine_mapping_tests PRIVATE soma_portable)

    add_executable(signature_scan_tests tests/signature_scan_tests.cpp)
    target_link_libraries(signature_scan_tests PRIVATE soma_portable)

    add_executable(world_mapping_tests tests/world_mapping_tests.cpp)
    target_link_libraries(world_mapping_tests PRIVATE soma_portable)

    add_executable(world_interpolation_tests
        tests/world_interpolation_tests.cpp)
    target_link_libraries(world_interpolation_tests PRIVATE soma_portable)

    add_executable(scene_mapping_tests tests/scene_mapping_tests.cpp)
    target_link_libraries(scene_mapping_tests PRIVATE soma_portable)

    add_executable(render_unlock_tests
        tests/render_unlock_tests.cpp
        src/logger.cpp
        src/runtime.cpp
    )
    target_include_directories(render_unlock_tests PRIVATE src)
    target_compile_definitions(render_unlock_tests PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(render_unlock_tests PRIVATE soma_portable minhook)

    add_executable(config_tests tests/config_tests.cpp src/config.cpp)
    target_include_directories(config_tests PRIVATE src)
    target_compile_definitions(config_tests PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

    add_executable(asi_bootstrap_host tests/asi_bootstrap_host.cpp)
    target_compile_definitions(asi_bootstrap_host PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

    foreach(test_target IN ITEMS
            agent_mapping_tests
            presentation_clock_tests
            camera_interpolation_tests
            frame_cadence_tests
            hands_mapping_tests
            mouse_look_preview_tests
            gui_mouse_mapping_tests
            hands_interpolation_tests
            pose_interpolation_tests
            engine_mapping_tests
            scene_mapping_tests
            signature_scan_tests
            world_mapping_tests
            world_interpolation_tests
            render_unlock_tests
            config_tests
            asi_bootstrap_host)
        target_compile_features(${test_target} PRIVATE cxx_std_20)
        target_compile_options(${test_target} PRIVATE
            /W4 /WX /permissive- /utf-8 /Zc:preprocessor)
        set_property(TARGET ${test_target} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        set_target_properties(${test_target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endforeach()

    add_test(NAME presentation_clock COMMAND presentation_clock_tests)
    add_test(NAME agent_mapping COMMAND agent_mapping_tests)
    add_test(NAME camera_interpolation COMMAND camera_interpolation_tests)
    add_test(NAME frame_cadence COMMAND frame_cadence_tests)
    add_test(NAME hands_mapping COMMAND hands_mapping_tests)
    add_test(NAME mouse_look_preview COMMAND mouse_look_preview_tests)
    add_test(NAME gui_mouse_mapping COMMAND gui_mouse_mapping_tests)
    add_test(NAME hands_interpolation COMMAND hands_interpolation_tests)
    add_test(NAME pose_interpolation COMMAND pose_interpolation_tests)
    add_test(NAME engine_mapping COMMAND engine_mapping_tests)
    add_test(NAME scene_mapping COMMAND scene_mapping_tests)
    add_test(NAME signature_scan COMMAND signature_scan_tests)
    add_test(NAME world_mapping COMMAND world_mapping_tests)
    add_test(NAME world_interpolation COMMAND world_interpolation_tests)
    add_test(NAME render_unlock COMMAND render_unlock_tests)
    add_test(NAME config COMMAND config_tests)
    add_test(
        NAME asi_bootstrap_rejects_incompatible_host
        COMMAND asi_bootstrap_host "$<TARGET_FILE:SomaFpsFix>"
    )
endif()

