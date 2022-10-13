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

add_executable(acc-engineer-server
  main.cpp ${PROTO_SRCS} ${PROTO_HDRS}
        config.h config.cpp service.cpp service.h)

add_executable(acc-engineer-server-test
        test/main.cpp ${PROTO_SRCS} ${PROTO_HDRS}
        config.h config.cpp service.cpp service.h)

target_link_libraries(acc-engineer-server Qt${QT_VERSION_MAJOR}::Core)
target_link_libraries(acc-engineer-server Boost::boost Boost::program_options)
target_link_libraries(acc-engineer-server protobuf::libprotobuf)

target_link_libraries(acc-engineer-server-test Qt${QT_VERSION_MAJOR}::Core)
target_link_libraries(acc-engineer-server-test Boost::boost Boost::program_options)
target_link_libraries(acc-engineer-server-test protobuf::libprotobuf)

target_include_directories(acc-engineer-server PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(acc-engineer-server PRIVATE BOOST_ASIO_NO_DEPRECATED)

target_include_directories(acc-engineer-server-test PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(acc-engineer-server-test PRIVATE BOOST_ASIO_NO_DEPRECATED)