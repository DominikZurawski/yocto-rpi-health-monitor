cmake_minimum_required(VERSION 3.16)
project(SentinelMetrics VERSION 1.0.0 LANGUAGES C)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

add_library(sentinel-metrics SHARED src/metrics.c)
set_target_properties(sentinel-metrics PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME metrics
)
target_compile_definitions(sentinel-metrics PRIVATE _POSIX_C_SOURCE=200809L)
set(SENTINEL_COMMON_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../common" CACHE PATH
    "Katalog prywatnych nagłówków parsowania")
target_include_directories(sentinel-metrics PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE "${SENTINEL_COMMON_INCLUDE_DIR}"
)

set(package_config_dir "${CMAKE_INSTALL_LIBDIR}/cmake/SentinelMetrics")
write_basic_package_version_file(SentinelMetricsConfigVersion.cmake
    COMPATIBILITY SameMajorVersion
)
install(TARGETS sentinel-metrics EXPORT SentinelMetricsTargets
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(FILES include/sentinel/metrics.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sentinel")
install(EXPORT SentinelMetricsTargets
    FILE SentinelMetricsConfig.cmake
    NAMESPACE SentinelMetrics::
    DESTINATION "${package_config_dir}"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SentinelMetricsConfigVersion.cmake"
    DESTINATION "${package_config_dir}"
)
