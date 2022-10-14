function(ADD_ACC_ENGINEER_TEST TEST_NAME TEST_SRCS)
    add_executable(${TEST_NAME} ${TEST_SRCS} ${ACC_ENGINEER_SERVER_TEST_PROTO_SRCS} ${ACC_ENGINEER_SERVER_TEST_SRCS})
    target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${ACC_ENGINEER_SERVER_INCLUDE_DIRS})
    target_compile_definitions(${TEST_NAME} PRIVATE BOOST_ASIO_NO_DEPRECATED)
    target_link_libraries(${TEST_NAME} PRIVATE Boost::boost Boost::program_options)
    target_link_libraries(${TEST_NAME} PRIVATE protobuf::libprotobuf)

    if (MSVC)
        target_compile_definitions(${TEST_NAME} PRIVATE _WIN32_WINNT=0x0A00)
    endif ()
endfunction()

ADD_ACC_ENGINEER_TEST(test_channel test_channel.cpp)
ADD_ACC_ENGINEER_TEST(test_rpc test_rpc.cpp)