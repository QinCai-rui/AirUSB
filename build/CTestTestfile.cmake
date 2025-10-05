# CMake generated Testfile for 
# Source directory: /home/qincai/AirUSB
# Build directory: /home/qincai/AirUSB/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(protocol_test "/home/qincai/AirUSB/build/airusb-benchmark" "--test-protocol")
set_tests_properties(protocol_test PROPERTIES  _BACKTRACE_TRIPLES "/home/qincai/AirUSB/CMakeLists.txt;91;add_test;/home/qincai/AirUSB/CMakeLists.txt;0;")
add_test(bandwidth_test "/home/qincai/AirUSB/build/airusb-benchmark" "--test-bandwidth")
set_tests_properties(bandwidth_test PROPERTIES  _BACKTRACE_TRIPLES "/home/qincai/AirUSB/CMakeLists.txt;93;add_test;/home/qincai/AirUSB/CMakeLists.txt;0;")
add_test(latency_test "/home/qincai/AirUSB/build/airusb-benchmark" "--test-latency")
set_tests_properties(latency_test PROPERTIES  _BACKTRACE_TRIPLES "/home/qincai/AirUSB/CMakeLists.txt;95;add_test;/home/qincai/AirUSB/CMakeLists.txt;0;")
