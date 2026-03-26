cmake_minimum_required(VERSION 3.10)
project(irc-parser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_library(ircp_lib STATIC
    src/ircp.cpp
    src/irc_connection.cpp
)

target_include_directories(ircp_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(ircp_lib PUBLIC Threads::Threads)

add_executable(irc-parser src/main.cpp)
target_link_libraries(irc-parser PRIVATE ircp_lib)
