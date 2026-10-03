cmake_minimum_required(VERSION 3.16)
project(SpecQR VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
option(SPECQR_BUILD_TESTS "Build dependency-free tests" ON)
option(SPECQR_BUILD_CLI "Build specqr-cli" ON)
option(SPECQR_BUILD_EXAMPLES "Build examples" ON)
set(SPECQR_SANITIZER "" CACHE STRING "Sanitizer: address-undefined or thread")
set_property(CACHE SPECQR_SANITIZER PROPERTY STRINGS "" address-undefined thread)
if(MSVC)
  add_compile_options(/utf-8)
endif()

add_library(specqr
  src/core.cpp src/segments.cpp src/tables.cpp src/matrix.cpp
  src/gs1.cpp src/render.cpp src/structured_append.cpp src/scan.cpp)
add_library(specqr::specqr ALIAS specqr)
target_compile_features(specqr PUBLIC cxx_std_17)
set_target_properties(specqr PROPERTIES CXX_EXTENSIONS OFF
  WINDOWS_EXPORT_ALL_SYMBOLS ON VERSION ${PROJECT_VERSION} SOVERSION 0)
target_include_directories(specqr PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(MSVC)
  target_compile_options(specqr PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(specqr PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
if(SPECQR_SANITIZER)
  if(MSVC)
    message(FATAL_ERROR "SPECQR_SANITIZER requires GCC or Clang")
  elseif(SPECQR_SANITIZER STREQUAL "address-undefined")
    set(_specqr_sanitize -fsanitize=address,undefined -fno-omit-frame-pointer)
  elseif(SPECQR_SANITIZER STREQUAL "thread")
    set(_specqr_sanitize -fsanitize=thread -fno-omit-frame-pointer)
  else()
    message(FATAL_ERROR "Unknown SPECQR_SANITIZER value")
  endif()
  target_compile_options(specqr PUBLIC ${_specqr_sanitize})
  target_link_options(specqr PUBLIC ${_specqr_sanitize})
endif()

if(SPECQR_BUILD_CLI)
  add_executable(specqr-cli cli/main.cpp)
  target_link_libraries(specqr-cli PRIVATE specqr::specqr)
endif()
if(SPECQR_BUILD_EXAMPLES)
  add_executable(specqr-example examples/basic.cpp)
  target_link_libraries(specqr-example PRIVATE specqr::specqr)
endif()
if(SPECQR_BUILD_TESTS)
  enable_testing()
  find_package(Threads REQUIRED)
  add_executable(specqr-tests tests/tests.cpp)
  target_link_libraries(specqr-tests PRIVATE specqr::specqr Threads::Threads)
  add_test(NAME specqr-unit COMMAND specqr-tests)
  add_executable(specqr-conformance tests/conformance.cpp)
  target_link_libraries(specqr-conformance PRIVATE specqr::specqr)
  target_include_directories(specqr-conformance PRIVATE src)
  add_executable(specqr-gs1-tests tests/gs1_tests.cpp)
  target_link_libraries(specqr-gs1-tests PRIVATE specqr::specqr Threads::Threads)
  add_test(NAME specqr-gs1 COMMAND specqr-gs1-tests)
  add_executable(specqr-extension-tests tests/extensions_tests.cpp)
  target_link_libraries(specqr-extension-tests PRIVATE specqr::specqr)
  add_test(NAME specqr-extensions COMMAND specqr-extension-tests)
  add_executable(specqr-core-edge-tests tests/core_edge_tests.cpp)
  target_link_libraries(specqr-core-edge-tests PRIVATE specqr::specqr)
  target_include_directories(specqr-core-edge-tests PRIVATE src)
  add_test(NAME specqr-core-edges COMMAND specqr-core-edge-tests)
  add_executable(specqr-review-tests tests/review_tests.cpp)
  target_link_libraries(specqr-review-tests PRIVATE specqr::specqr)
  add_test(NAME specqr-review COMMAND specqr-review-tests)
endif()

install(TARGETS specqr EXPORT SpecQRTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(SPECQR_BUILD_CLI)
  install(TARGETS specqr-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/specqr DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT SpecQRTargets FILE SpecQRTargets.cmake NAMESPACE specqr::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SpecQR)
configure_package_config_file(cmake/SpecQRConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/SpecQRConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SpecQR)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/SpecQRConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/SpecQRConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/SpecQRConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SpecQR)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/SpecQR)
