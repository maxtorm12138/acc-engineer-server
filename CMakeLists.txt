cmake_minimum_required(VERSION 3.14)

project(acc-engineer-server LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.80.0 COMPONENTS program_options REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Protobuf REQUIRED)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS proto/service.proto)

set(ACC_ENGINEER_SERVER_SRCS
        src/detail/rpc/method.h
        src/config.cpp
        src/config.h
        src/rpc.cpp
        src/rpc.h
        src/service.cpp
        src/service.h
        src/main.cpp)

set(ACC_ENGINEER_SERVER_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/src/ ${CMAKE_CURRENT_BINARY_DIR})
set(ACC_ENGINEER_SERVER_TEST_PROTO_SRCS ${PROTO_SRCS} ${PROTO_HDRS})
set(ACC_ENGINEER_SERVER_TEST_SRCS ${CMAKE_SOURCE_DIR}/src/config.cpp ${CMAKE_SOURCE_DIR}/src/rpc.cpp)

add_executable(acc-engineer-server ${ACC_ENGINEER_SERVER_SRCS} ${PROTO_SRCS} ${PROTO_HDRS})

target_link_libraries(acc-engineer-server Boost::boost Boost::program_options)
target_link_libraries(acc-engineer-server protobuf::libprotobuf)

target_include_directories(acc-engineer-server PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_compile_definitions(acc-engineer-server PRIVATE BOOST_ASIO_NO_DEPRECATED)

if (MSVC)
    target_compile_definitions(acc-engineer-server PRIVATE _WIN32_WINNT=0x0A00)
endif()


if (NOT ACC_ENGINEER_NO_BUILD_TESTS)
    add_subdirectory(test/)
endif()
