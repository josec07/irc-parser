cmake_minimum_required(VERSION 3.10)
project(irc-parser VERSION 1.0.0)  # Replace with your actual project name

# Set CPack project name
set(CPACK_PROJECT_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION "A cross-platform application for IRC communication")
set(CPACK_PACKAGE_VENDOR "Jose Caudillo")
set(CPACK_PACKAGE_CONTACT "caudillojose5@gmail.com")
set(CPACK_GENERATOR "TGZ;NSIS;DMG")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)



include(CPack)
add_library(ircp_lib STATIC
    src/session.cpp
    src/twitch_irc.cpp
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
