cmake_minimum_required(VERSION 3.12)

project(CannedYAML)

set(CMAKE_CXX_STANDARD 17)
include_directories(include)

include(ExternalProject)
set(EP_BASE extern)

ExternalProject_Add(YAML_CPP
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG yaml-cpp-0.6.2
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_HOME_DIRECTORY}
)

find_library(LIB_YAML "libyaml-cpp.a" PATHS "lib")

ExternalProject_Add(LibSWOC
        GIT_REPOSITORY https://github.com/solidwallofcode/libswoc.git
        GIT_TAG master
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_HOME_DIRECTORY}
)

find_library(LIB_SWOC "libswoc++.a" PATHS "lib")

find_library(POSIX_THREAD_LIB pthread)

link_libraries(${LIB_YAML} ${LIB_SWOC} ${POSIX_THREAD_LIB})

add_executable(canner
    src/canner.cc
)
