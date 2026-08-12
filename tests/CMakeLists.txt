include(FetchContent)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    GIT_SHALLOW ON
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(hive_test hive_test.cpp)
target_link_libraries(hive_test PRIVATE nlib::nlib GTest::gtest_main)
gtest_discover_tests(hive_test)

add_executable(single_queue_test single_queue_test.cpp)
target_link_libraries(single_queue_test PRIVATE nlib::nlib GTest::gtest_main)
gtest_discover_tests(single_queue_test)

add_executable(pool_test pool_test.cpp)
target_link_libraries(pool_test PRIVATE nlib::nlib GTest::gtest_main)
gtest_discover_tests(pool_test)
