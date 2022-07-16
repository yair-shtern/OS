cmake_minimum_required(VERSION 3.17)
project(osm)

set(CMAKE_CXX_STANDARD 14)

add_executable(osm osm.cpp main.cpp osm.h)
