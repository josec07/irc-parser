cmake_minimum_required(VERSION 3.10)
project(irc-parser VERSION 1.0.0)  # Replace with your actual project name

# Set CPack project name
set(CPACK_PROJECT_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION "A cross-platform application for IRC communication")
set(CPACK_PACKAGE_VENDOR "YourName")
set(CPACK_PACKAGE_CONTACT "you@example.com")
set(CPACK_GENERATOR "TGZ;NSIS;DMG")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)



include(CPack)
add_library(ircp_lib STATIC
    src/ircp.cpp
    src/irc_connection.cpp
    src/logger.cpp
)

target_include_directories(ircp_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(ircp_lib PUBLIC Threads::Threads)

add_executable(irc-parser src/main.cpp)
target_link_libraries(irc-parser PRIVATE ircp_lib)
