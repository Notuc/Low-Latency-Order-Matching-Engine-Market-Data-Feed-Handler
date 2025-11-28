# In test/CMakeLists.txt

# Define the test executable
add_executable(commontest common/commontest.cpp)
add_executable(test_l2_apply test_l2_apply.cpp)
# Link the test executable to the GTest libraries
target_link_libraries(commontest PRIVATE gtest_main)
target_link_libraries(test_l2_apply PRIVATE trading_engine gtest_main)


add_test(NAME test_l2_apply COMMAND test_l2_apply)

# Enable testing in CMake and discover the tests
enable_testing()
include(GoogleTest)
gtest_discover_tests(commontest)
