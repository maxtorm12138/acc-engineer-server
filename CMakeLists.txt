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

FUNCTION(GENERATE_PROTO PROTO_ROOT PROTO_FILE GENERATE_PATH OUT_FILES)
    GET_FILENAME_COMPONENT(PROTO_FILE ${PROTO_FILE} ABSOLUTE)
    GET_FILENAME_COMPONENT(PROTO_ROOT ${PROTO_ROOT} ABSOLUTE)
    get_filename_component(PROTO_FILE_NAME ${PROTO_FILE} NAME)

    string(REPLACE ".proto" ".pb.cc" PROTO_CPP_SRC "${GENERATE_PATH}/${PROTO_FILE_NAME}")
    string(REPLACE ".proto" ".pb.h" PROTO_CPP_HDR "${GENERATE_PATH}/${PROTO_FILE_NAME}")
    
    FILE(TO_NATIVE_PATH ${PROTO_FILE} NATIVE_PROTO_FILE)
    FILE(TO_NATIVE_PATH ${GENERATE_PATH} NATIVE_GENERATE_PATH)
    FILE(TO_NATIVE_PATH ${PROTO_ROOT} NATIVE_PROTO_ROOT)

    FILE(MAKE_DIRECTORY ${NATIVE_GENERATE_PATH})

    EXECUTE_PROCESS(COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} --proto_path=${NATIVE_PROTO_ROOT} --cpp_out=${NATIVE_GENERATE_PATH} ${NATIVE_PROTO_FILE} RESULT_VARIABLE RV)
     
    IF(${RV})
        MESSAGE(FATAL_ERROR "Generation of data model returned ${RV} for proto ${NATIVE_PROTO_FILE}")
    ENDIF()
    set(${OUT_FILES} ${PROTO_CPP_SRC} ${PROTO_CPP_HDR} PARENT_SCOPE)

ENDFUNCTION()

GENERATE_PROTO(proto/ proto/rpc.proto ${CMAKE_CURRENT_BINARY_DIR}/proto RPC_PROTO_FILES)
GENERATE_PROTO(proto/ proto/service.proto ${CMAKE_CURRENT_BINARY_DIR}/proto SERVICE_PROTO_FILES)

set(ACC_ENGINEER_PROTO_INCLUDE_DIR
        ${CMAKE_CURRENT_BINARY_DIR})

set(ACC_ENGINEER_SERVER_RPC_SRCS
        src/rpc/detail/method_type_erasure.h
        src/rpc/methods.h
        src/rpc/result.h
        src/rpc/stub.h
        src/rpc/error_code.h
        ${RPC_PROTO_FILES})

set(ACC_ENGINEER_SERVER_SRCS
        ${ACC_ENGINEER_SERVER_RPC_SRCS}
        ${ACC_ENGINEER_PROTO_SRCS}
        src/config.cpp
        src/config.h
        src/service.cpp
        src/service.h
        src/main.cpp)

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
