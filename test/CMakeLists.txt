cmake_minimum_required(VERSION 3.5)

set(Boost_USE_MULTI_THREADED ON)
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost
	REQUIRED
    unit_test_framework
    system
    thread
    date_time
    chrono
)

function(create_test test_name test_source)
	add_executable(${test_name} ${test_source})
	target_include_directories(${test_name} PRIVATE ${Boost_INCLUDE_DIR} ../include)
	target_link_libraries(${test_name} PUBLIC ${Boost_LIBRARIES})
	add_test(${test_name} ${test_name})
	#set_property(TARGET ${test_name} APPEND PROPERTY COMPILE_DEFINITIONS "ASIO_STANDALONE=1;ASIO_MSVC=1900;ASIO_HAS_VARIADIC_TEMPLATES=1;ASIO_DELETED=1")
endfunction(create_test)

create_test(test.future future.cpp)
create_test(test.packaged_task packaged_task.cpp)