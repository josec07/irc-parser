cmake_minimum_required(VERSION 3.16)
project(irc-parser VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_library(ircp STATIC
    src/util.cpp
    src/logger.cpp
    src/config.cpp
    src/irc_message.cpp
    src/analytics.cpp
    src/tls_session.cpp
    src/twitch_client.cpp
    src/http_server.cpp
    src/demo.cpp
    src/app.cpp
)

target_include_directories(ircp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(ircp SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ircp PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_compile_options(ircp PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(irc-parser src/main.cpp)
target_link_libraries(irc-parser PRIVATE ircp)
target_compile_options(irc-parser PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(ircp-tests tests/tests.cpp)
target_link_libraries(ircp-tests PRIVATE ircp)
target_compile_options(ircp-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

enable_testing()
add_test(NAME ircp-tests COMMAND ircp-tests)

install(TARGETS irc-parser RUNTIME DESTINATION bin)
