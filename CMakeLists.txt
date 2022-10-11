cmake_minimum_required(VERSION 3.14)

project(acc-engineer-server LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost COMPONENTS REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(CapnProto REQUIRED)

add_executable(acc-engineer-server
  main.cpp
)

target_link_libraries(acc-engineer-server Qt${QT_VERSION_MAJOR}::Core)
target_link_libraries(acc-engineer-server Boost::boost)
target_link_libraries(acc-engineer-server CapnProto::capnp)
target_compile_definitions(acc-engineer-server PRIVATE BOOST_ASIO_NO_DEPRECATED)

install(TARGETS acc-engineer-server
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
