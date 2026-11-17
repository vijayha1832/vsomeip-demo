cmake_minimum_required(VERSION 3.14)
project(someip_demo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Locate vsomeip3 (installed via apt or built from source)
find_package(vsomeip3 REQUIRED)
find_package(Threads REQUIRED)

add_executable(someip_service src/someip_service.cpp)
target_link_libraries(someip_service
    vsomeip3
    Threads::Threads
)

add_executable(someip_client src/someip_client.cpp)
target_link_libraries(someip_client
    vsomeip3
    Threads::Threads
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(someip_service PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(someip_client PRIVATE -Wall -Wextra -Wpedantic)
endif()
