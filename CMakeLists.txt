cmake_minimum_required(VERSION 3.12)
set(CMAKE_CXX_STANDARD 17)

set(INSTALL_DIR ${CMAKE_HOME_DIRECTORY})
if(NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH ${INSTALL_DIR})
endif()

include(ExternalProject)
set(EP_BASE extern)

# All of the arguments to pass to external projects.
set(_CMAKE_ARGS
    CMAKE_INSTALL_PREFIX=${INSTALL_DIR}
    CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
    YAML_CPP_BUILD_TESTS=off
    YAML_CPP_BUILD_TOOLS=off
    YAML_CPP_BUILD_CONTRIB=off
    )
list(TRANSFORM _CMAKE_ARGS PREPEND "-D")

set(_YAML_CPP_ARGS
    YAML_CPP_BUILD_TESTS=off
    YAML_CPP_BUILD_TOOLS=off
    YAML_CPP_BUILD_CONTRIB=off
    )
list(TRANSFORM _YAML_CPPR_ARGS PREPEND "-D")

ExternalProject_Add(lib-yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG yaml-cpp-0.6.2
    GIT_SHALLOW TRUE
    INSTALL_DIR ${INSTALL_DIR}
    CMAKE_ARGS "${_CMAKE_ARGS};${_YAML_CPP_ARGS}"
    STEP_TARGETS install
    )

ExternalProject_Add(lib-swoc++
    GIT_REPOSITORY https://github.com/solidwallofcode/libswoc.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR swoc++
    INSTALL_DIR ${INSTALL_DIR}
    CMAKE_ARGS "${_CMAKE_ARGS}"
    STEP_TARGETS install
    )

add_custom_target(clang-format COMMAND ${CMAKE_HOME_DIRECTORY}/tools/clang-format.sh canner/src)

# If using CLion, pass "-DENABLE_IDE_HACKS=TRUE" as a CMake argument to make the editor behave as desired.
if(${ENABLE_IDE_HACKS})
    add_subdirectory(canner)
endif()
