cmake_minimum_required(VERSION 3.14)

project(acc-engineer-server LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(Protobuf_USE_STATIC_LIBS ON)

find_package(Boost 1.80.0 COMPONENTS program_options REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Protobuf REQUIRED)
find_package(spdlog REQUIRED)

function(generate_proto PROTO_ROOT PROTO_FILE GENERATE_PATH OUT_FILES)
    get_filename_component(PROTO_FILE ${PROTO_FILE} ABSOLUTE)
    get_filename_component(PROTO_ROOT ${PROTO_ROOT} ABSOLUTE)
    get_filename_component(PROTO_FILE_NAME ${PROTO_FILE} NAME)

    string(REPLACE ".proto" ".pb.cc" PROTO_CPP_SRC "${GENERATE_PATH}/${PROTO_FILE_NAME}")
    string(REPLACE ".proto" ".pb.h" PROTO_CPP_HDR "${GENERATE_PATH}/${PROTO_FILE_NAME}")

    file(TO_NATIVE_PATH ${PROTO_FILE} NATIVE_PROTO_FILE)
    file(TO_NATIVE_PATH ${GENERATE_PATH} NATIVE_GENERATE_PATH)
    file(TO_NATIVE_PATH ${PROTO_ROOT} NATIVE_PROTO_ROOT)

    file(MAKE_DIRECTORY ${NATIVE_GENERATE_PATH})

    execute_process(COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} --proto_path=${NATIVE_PROTO_ROOT} --cpp_out=${NATIVE_GENERATE_PATH} ${NATIVE_PROTO_FILE} RESULT_VARIABLE RV)

    if(${RV})
        message(FATAL_ERROR "Generation of data model returned ${RV} for proto ${NATIVE_PROTO_FILE}")
    endif()
    set(${OUT_FILES} ${PROTO_CPP_SRC} ${PROTO_CPP_HDR} PARENT_SCOPE)

endfunction()

generate_proto(proto/ proto/rpc.proto ${CMAKE_CURRENT_BINARY_DIR}/proto RPC_PROTO_FILES)
generate_proto(proto/ proto/service.proto ${CMAKE_CURRENT_BINARY_DIR}/proto SERVICE_PROTO_FILES)

set(ACC_ENGINEER_PROTO_INCLUDE_DIR
        ${CMAKE_CURRENT_BINARY_DIR})

set(ACC_ENGINEER_SERVER_RPC_SRCS
        src/rpc/detail/method_type_erasure.h
        src/rpc/methods.h
        src/rpc/result.h
        src/rpc/stub.h
        src/rpc/error_code.h
        src/rpc/await_ec.h
        ${RPC_PROTO_FILES})

add_library(acc-engineer-server-rpc ${ACC_ENGINEER_SERVER_RPC_SRCS})
target_link_libraries(acc-engineer-server-rpc PRIVATE Boost::boost)
target_link_libraries(acc-engineer-server-rpc PRIVATE protobuf::libprotobuf)
target_link_libraries(acc-engineer-server-rpc PUBLIC spdlog::spdlog)
target_include_directories(acc-engineer-server-rpc PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(acc-engineer-server-rpc PRIVATE BOOST_ASIO_NO_DEPRECATED)
if (MSVC)
    target_compile_definitions(acc-engineer-server-rpc PRIVATE _WIN32_WINNT=0x0A00)
endif ()


set(ACC_ENGINEER_SERVER_SRCS
        ${SERVICE_PROTO_FILES}
        src/config.cpp
        src/config.h
        src/service.cpp
        src/service.h
        src/main.cpp)

add_executable(acc-engineer-server ${ACC_ENGINEER_SERVER_SRCS} ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(acc-engineer-server acc-engineer-server-rpc)
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
