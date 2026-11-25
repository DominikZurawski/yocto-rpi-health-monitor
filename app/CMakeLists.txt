cmake_minimum_required(VERSION 3.16)
project(rpi-sentinel VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CTest)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_compile_definitions(_POSIX_C_SOURCE=200809L)

add_library(sentinel_core STATIC src/config.c src/health.c src/metrics.c src/status.c)
target_include_directories(sentinel_core PUBLIC include)
add_executable(rpi-sentinel src/main.c)
target_link_libraries(rpi-sentinel PRIVATE sentinel_core PkgConfig::SYSTEMD)

install(TARGETS rpi-sentinel RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
set(SENTINEL_CONFIG_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/rpi-sentinel" CACHE STRING
    "Katalog instalacji pliku konfiguracyjnego")
install(FILES rpi-sentinel.conf DESTINATION "${SENTINEL_CONFIG_INSTALL_DIR}")

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(sentinel-unit-tests tests/unit.c)
    target_link_libraries(sentinel-unit-tests PRIVATE sentinel_core)
    add_test(NAME sentinel-unit COMMAND sentinel-unit-tests)
    add_test(NAME sentinel-process
             COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/process.sh" $<TARGET_FILE:rpi-sentinel>)
    add_test(NAME sentinel-shutdown
             COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/process.sh" $<TARGET_FILE:rpi-sentinel> shutdown)
    add_test(NAME sentinel-watchdog
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/watchdog.py"
                     $<TARGET_FILE:rpi-sentinel>)
    set_tests_properties(sentinel-process PROPERTIES TIMEOUT 25)
    set_tests_properties(sentinel-shutdown PROPERTIES TIMEOUT 6)
    set_tests_properties(sentinel-watchdog PROPERTIES TIMEOUT 15)
endif()
