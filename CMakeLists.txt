cmake_minimum_required(VERSION 3.10)
project(TwitchIRC CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(twitch-irc
    src/twitch_irc.cpp
)

target_include_directories(twitch-irc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# TwitchIRC requires Boost Asio (via IrcConnection if kept) and IrcConnection itself
# For now, we expect the consumer to provide IrcConnection or we'll add it later.
