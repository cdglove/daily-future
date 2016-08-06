cmake_minimum_required(VERSION 3.5)

include(external_git.cmake)

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

clone_external_git_repo(
	URL "https://github.com/chriskohlhoff/executors.git"
	TAG "master"
	TARGET_DIR "${CMAKE_BINARY_DIR}/executors")

function(create_test test_name test_source)
	add_executable(${test_name} ${test_source})
	target_include_directories(${test_name} PRIVATE ${Boost_INCLUDE_DIR} "${CMAKE_BINARY_DIR}/executors/include")
	target_link_libraries(${test_name} PUBLIC daily_future ${Boost_LIBRARIES})
	add_test(${test_name} ${test_name})
endfunction(create_test)

create_test(test.future future.cpp)
create_test(test.use_future use_future.cpp)
create_test(test.packaged_task packaged_task.cpp)
create_test(test.allocator_support allocator_support.cpp)