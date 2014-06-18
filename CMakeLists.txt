cmake_minimum_required(VERSION 2.6)

project(netmeasured C)
add_definitions(-Os -Wall -Werror --std=gnu99 -Wmissing-declarations)

set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")

set(SOURCES
  main.c
)

set(LIBS
  ubox
  ubus
  uci
  m
)

include_directories(include)

# Netmeasured binary
add_executable(netmeasured ${SOURCES})
target_link_libraries(netmeasured ${LIBS})
