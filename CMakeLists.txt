cmake_minimum_required(VERSION 3.20)
project(HXrag LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "HXrag currently targets Windows (WinHTTP + Winsock).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(hxrag
    src/main.cpp
    src/config.cpp
    src/http_client.cpp
    src/rag.cpp
    src/server.cpp
)

target_include_directories(hxrag PRIVATE src third_party)
target_link_libraries(hxrag PRIVATE winhttp ws2_32 shell32)

if(MSVC)
    target_compile_options(hxrag PRIVATE /W4 /utf-8)
else()
    target_compile_options(hxrag PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(hxrag PRIVATE -static -static-libgcc -static-libstdc++)
endif()
