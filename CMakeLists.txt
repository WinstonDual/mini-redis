cmake_minimum_required(VERSION 3.16)
project(mini_redis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# --- Платформенные библиотеки ---
if(WIN32)
    set(PLATFORM_LIBS ws2_32)
else()
    set(PLATFORM_LIBS pthread)
endif()

# --- Библиотека с общей логикой (net + resp + server) ---
add_library(miniredis_core STATIC
    src/net/socket.cpp
    src/resp/serializer.cpp
    src/resp/parser.cpp
    src/server/command.cpp
)

target_include_directories(miniredis_core PUBLIC src)
target_link_libraries(miniredis_core PUBLIC ${PLATFORM_LIBS})

# --- Основной исполняемый файл ---
add_executable(mini-redis src/main.cpp)
target_link_libraries(mini-redis PRIVATE miniredis_core)

# --- Тесты ---
add_executable(mini-redis-tests
    src/tests/test_main.cpp
    src/tests/test_serializer.cpp
    src/tests/test_parser.cpp
    src/tests/test_command.cpp
)
target_link_libraries(mini-redis-tests PRIVATE miniredis_core)

# --- Warnings ---
if(MSVC)
    target_compile_options(miniredis_core     PRIVATE /W4)
    target_compile_options(mini-redis         PRIVATE /W4)
    target_compile_options(mini-redis-tests   PRIVATE /W4)
else()
    target_compile_options(miniredis_core     PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(mini-redis         PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(mini-redis-tests   PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- CTest ---
enable_testing()
add_test(NAME all_tests COMMAND mini-redis-tests)