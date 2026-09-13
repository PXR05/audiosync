cmake_minimum_required(VERSION 3.16)

set(AUDIOSYNC_VERSION "0.1.1" CACHE STRING "AudioSync release version")
project(audiosync VERSION ${AUDIOSYNC_VERSION} LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(AUDIOSYNC_TRAY "Build Linux desktop tray support" ON)
option(AUDIOSYNC_EDITOR_CONFIG "Update the source-root compilation database for editors" ON)
file(GLOB ADAPTER_SOURCES CONFIGURE_DEPENDS src/adapters/*.c)

if(WIN32)
    set(PLATFORM windows)
    enable_language(RC)
    add_compile_definitions(UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    set(PLATFORM_EXTRA src/platform/windows/crashlog.c)
    set(PLATFORM_LIBRARIES winhttp shell32 ole32 uuid crypt32 setupapi dbghelp advapi32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM linux)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(AUDIOSYNC_ARCH aarch64)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        set(AUDIOSYNC_ARCH x86_64)
    else()
        message(FATAL_ERROR "Unsupported Linux architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    include(GNUInstallDirs)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
    pkg_check_modules(SECRET REQUIRED IMPORTED_TARGET libsecret-1)
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
    set(PLATFORM_EXTRA src/platform/linux/compat.c)
    set(PLATFORM_LIBRARIES PkgConfig::CURL PkgConfig::SECRET PkgConfig::GIO pthread)
else()
    message(FATAL_ERROR "AudioSync supports Windows and Linux; this OS needs a platform backend.")
endif()

add_library(audiosync_lib STATIC
    src/config.c src/organizer.c src/progress.c src/status.c src/json.c src/log.c
    ${ADAPTER_SOURCES}
    src/platform/files.c src/platform/runtime.c src/platform/devices.c
    src/platform/${PLATFORM}/convert.c
    src/platform/${PLATFORM}/devices.c
    src/platform/${PLATFORM}/runtime.c
    src/platform/${PLATFORM}/http.c
    src/platform/${PLATFORM}/tray.c
    src/platform/${PLATFORM}/util.c
    ${PLATFORM_EXTRA})
target_include_directories(audiosync_lib PUBLIC src)
target_compile_options(audiosync_lib PRIVATE -Wall -Wextra)
target_link_libraries(audiosync_lib PUBLIC ${PLATFORM_LIBRARIES})

add_executable(audiosync
    src/main.c src/sync.c
    src/cli/cli.c src/cli/help.c src/cli/profile.c
    src/cli/output.c src/cli/observe.c src/cli/progress.c src/cli/update.c
    src/platform/${PLATFORM}/detach.c src/platform/${PLATFORM}/update.c)
target_compile_definitions(audiosync PRIVATE AUDIOSYNC_VERSION="${PROJECT_VERSION}")
target_compile_options(audiosync PRIVATE -Wall -Wextra)
target_link_libraries(audiosync PRIVATE audiosync_lib)

if(WIN32)
    target_sources(audiosync PRIVATE packaging/windows/app.rc)
    target_include_directories(audiosync PRIVATE packaging/windows)
    if(MINGW)
        target_link_options(audiosync PRIVATE -static $<$<CONFIG:Release>:-s>)
    endif()
else()
    if(AUDIOSYNC_TRAY)
        pkg_check_modules(APPINDICATOR REQUIRED IMPORTED_TARGET ayatana-appindicator3-0.1)
        target_compile_definitions(audiosync_lib PRIVATE AUDIOSYNC_TRAY)
        target_link_libraries(audiosync_lib PUBLIC PkgConfig::APPINDICATOR)
    endif()
    install(FILES README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/audiosync
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    install(FILES docs/cli.md docs/adapters.md docs/development.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/audiosync/docs
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    install(TARGETS audiosync RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    if(AUDIOSYNC_TRAY)
        install(FILES packaging/linux/audiosync.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
            PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
        install(FILES packaging/linux/audiosync.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    endif()
endif()

if(AUDIOSYNC_EDITOR_CONFIG AND CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
    add_custom_target(editor_config ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/compile_commands.json" "${CMAKE_SOURCE_DIR}/compile_commands.json"
        COMMENT "Updating editor compilation database"
        VERBATIM)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(sync_test tests/sync_test.c)
    add_executable(json_test tests/json_test.c)
    add_executable(adapter_test tests/adapter_test.c)
    add_executable(progress_fixture tests/progress_fixture.c)
    foreach(target sync_test json_test adapter_test progress_fixture)
        target_compile_options(${target} PRIVATE -UNDEBUG -Wall -Wextra)
        target_link_libraries(${target} PRIVATE audiosync_lib)
    endforeach()
    add_test(NAME unit_json COMMAND json_test)
    add_test(NAME unit_adapter COMMAND adapter_test)
    set_tests_properties(unit_json unit_adapter PROPERTIES LABELS unit)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME regression_sync COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/run_sync_test.py $<TARGET_FILE:sync_test>)
        add_test(NAME regression_cli COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/cli_test.py $<TARGET_FILE:audiosync> $<TARGET_FILE:progress_fixture>)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_test(NAME regression_detach COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/detach_test_linux.py $<TARGET_FILE:audiosync>)
            set_tests_properties(regression_detach PROPERTIES TIMEOUT 20 LABELS integration)
        endif()
        set_tests_properties(regression_cli regression_sync PROPERTIES TIMEOUT 60 LABELS integration)
    else()
        add_test(NAME regression_sync COMMAND sync_test)
        set_tests_properties(regression_sync PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS integration)
    endif()
endif()

if(NOT WIN32)
    set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    set(CPACK_PACKAGE_NAME "AudioSync")
    set(CPACK_PACKAGE_VENDOR "AudioSync")
    set(CPACK_PACKAGE_CONTACT "AudioSync maintainers")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native audio synchronization utility")
    set(CPACK_PACKAGE_FILE_NAME "AudioSync-${PROJECT_VERSION}-linux-${AUDIOSYNC_ARCH}")
    set(CPACK_DEBIAN_FILE_NAME "AudioSync-${PROJECT_VERSION}-linux-${AUDIOSYNC_ARCH}.deb")
    set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_GENERATOR "DEB;TGZ")
    include(CPack)
endif()
