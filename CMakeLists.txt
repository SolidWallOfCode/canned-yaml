cmake_minimum_required(VERSION 3.12)

include(ExternalProject)
set(EP_BASE extern)
ExternalProject_Add(YAML_CPP
        PREFIX extern
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG yaml-cpp-0.6.2
        )
