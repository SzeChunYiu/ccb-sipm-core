cmake_minimum_required(VERSION 3.16)
project(ccb_sipm_core VERSION 0.1.0 LANGUAGES CXX)

option(CCB_SIPM_BUILD_TESTS "Build unit tests" ON)
option(CCB_SIPM_BUILD_EXAMPLES "Build examples" ON)
option(CCB_SIPM_WITH_GEANT4 "Build optional Geant4 adapter" OFF)

add_library(ccb_sipm_core
  src/Config.cc
  src/Seed.cc
  src/ResponseSimulator.cc
)
add_library(CCB::sipm_core ALIAS ccb_sipm_core)

target_include_directories(ccb_sipm_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(ccb_sipm_core PUBLIC cxx_std_17)
target_compile_options(ccb_sipm_core PRIVATE -Wall -Wextra -Wpedantic)

if(CCB_SIPM_BUILD_EXAMPLES)
  add_executable(ccb_sipm_toy examples/toy_sim.cc)
  target_link_libraries(ccb_sipm_toy PRIVATE CCB::sipm_core)
endif()

if(CCB_SIPM_BUILD_TESTS)
  enable_testing()
  add_executable(ccb_sipm_tests tests/test_core.cc)
  target_link_libraries(ccb_sipm_tests PRIVATE CCB::sipm_core)
  add_test(NAME ccb_sipm_core_tests COMMAND ccb_sipm_tests)
endif()

if(CCB_SIPM_WITH_GEANT4)
  find_package(Geant4 REQUIRED)
  add_library(ccb_sipm_geant4
    adapters/geant4/src/Geant4BoundaryCollector.cc
  )
  add_library(CCB::sipm_geant4 ALIAS ccb_sipm_geant4)
  target_compile_features(ccb_sipm_geant4 PUBLIC cxx_std_17)
  target_include_directories(ccb_sipm_geant4
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/adapters/geant4/include
      ${Geant4_INCLUDE_DIRS}
  )
  target_link_libraries(ccb_sipm_geant4 PUBLIC CCB::sipm_core ${Geant4_LIBRARIES})
  target_compile_options(ccb_sipm_geant4 PRIVATE -Wall -Wextra -Wpedantic)
endif()
