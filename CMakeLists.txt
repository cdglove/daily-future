cmake_minimum_required(VERSION 3.5)

option( DAILY_BUILD_TESTS "Build Daily Unit Tests" ON )
#if( DAILY_BUILD_TESTS )
  enable_testing()
#endif()

add_subdirectory(libs/future)

