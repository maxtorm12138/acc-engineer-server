add_executable(test_channel test_channel.cpp)

target_include_directories(test_channel PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_compile_definitions(test_channel PRIVATE BOOST_ASIO_NO_DEPRECATED)

target_link_libraries(test_channel PRIVATE Boost::boost Boost::program_options)

target_link_libraries(test_channel PRIVATE protobuf::libprotobuf)

if (MSVC)
    target_compile_definitions(test_channel PRIVATE _WIN32_WINNT=0x0A00)
endif()
