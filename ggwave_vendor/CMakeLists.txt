cmake_minimum_required(VERSION 3.16)
project(sonic_link_ggwave_vendor LANGUAGES CXX)

add_library(sonic_link_ggwave STATIC
    src/ggwave.cpp
)

target_include_directories(sonic_link_ggwave
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(sonic_link_ggwave PUBLIC cxx_std_11)
