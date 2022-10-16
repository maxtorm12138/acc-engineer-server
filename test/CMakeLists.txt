function(ADD_ACC_ENGINEER_TEST)
    cmake_parse_arguments(ACC_ENGINNER_TEST "" "NAME" "FILES" ${ARGN})    
    message("TEST ${ACC_ENGINNER_TEST_NAME} FILES: ${ACC_ENGINNER_TEST_FILES}")

    add_executable(${ACC_ENGINNER_TEST_NAME} ${ACC_ENGINNER_TEST_FILES})
    target_include_directories(${ACC_ENGINNER_TEST_NAME} PRIVATE ${ACC_ENGINEER_PROTO_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(${ACC_ENGINNER_TEST_NAME} PRIVATE BOOST_ASIO_NO_DEPRECATED)
    target_link_libraries(${ACC_ENGINNER_TEST_NAME} PRIVATE Boost::boost Boost::program_options)
    target_link_libraries(${ACC_ENGINNER_TEST_NAME} PRIVATE protobuf::libprotobuf)

    if (MSVC)
        target_compile_definitions(${ACC_ENGINNER_TEST_NAME} PRIVATE _WIN32_WINNT=0x0A00)
    endif ()
endfunction()

ADD_ACC_ENGINEER_TEST(NAME test_rpc FILES test_rpc.cpp ${RPC_PROTO_FILES} ${SERVICE_PROTO_FILES})
