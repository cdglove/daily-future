cmake_minimum_required(VERSION 3.5)
project(daily.future)


set(LIBRARY_NAME daily_future)

find_package(Boost)

add_library(${LIBRARY_NAME} INTERFACE)
file(GLOB_RECURSE PROJECT_HEADERS include/*.hpp)
target_sources(${LIBRARY_NAME} INTERFACE ${PROJECT_HEADERS})
target_include_directories(${LIBRARY_NAME} INTERFACE "include/")
target_link_libraries(${LIBRARY_NAME} INTERFACE Boost::boost)

set(Daily_Libraries ${Daily_Libraries} ${LIBRARY_NAME} PARENT_SCOPE)
if(DAILY_BUILD_TESTS)
	add_subdirectory(test)
endif()
