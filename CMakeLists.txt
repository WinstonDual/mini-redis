cmake_minimum_required(VERSION 3.16)
project(mini_redis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Windows: линкуем Winsock
if(WIN32)
    set(PLATFORM_LIBS ws2_32)
else()
    set(PLATFORM_LIBS pthread)
endif()

add_executable(mini-redis
    src/main.cpp
    src/net/socket.cpp
)

target_include_directories(mini-redis PRIVATE src)
target_link_libraries(mini-redis PRIVATE ${PLATFORM_LIBS})

# Warnings
if(MSVC)
    target_compile_options(mini-redis PRIVATE /W4)
else()
    target_compile_options(mini-redis PRIVATE -Wall -Wextra -Wpedantic)
endif()