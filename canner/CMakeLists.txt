cmake_minimum_required(VERSION 3.12)

project(canner)
set(CMAKE_CXX_STANDARD 17)
include(GNUInstallDirs)

find_package(swoc++ CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

add_executable(canner
    src/canner.cc
)

target_link_libraries(canner PUBLIC swoc++::swoc++ yaml-cpp)

install(TARGETS canner
    EXPORT canned-yaml-config
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
install(EXPORT canned-yaml-config
    NAMESPACE canned-yaml::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/canned-yaml
    )
export(TARGETS canner FILE canned-yaml-config.cmake)
