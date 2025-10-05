cmake_minimum_required(VERSION 3.16)
project(AirUSB VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

# Compiler flags for optimization
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# WiFi 6E optimizations
add_compile_definitions(
    WIFI6E_OPTIMIZED=1
    MAX_BANDWIDTH_GBPS=6
    LOW_LATENCY_MODE=1
)

# Include directories
include_directories(src)
include_directories(${LIBUSB_INCLUDE_DIRS})

# Common library
add_library(airusb_common STATIC
    src/protocol.cpp
)

target_link_libraries(airusb_common 
    ${LIBUSB_LIBRARIES}
    z  # zlib for compression
)

target_compile_options(airusb_common PRIVATE ${LIBUSB_CFLAGS_OTHER})

# Server executable
add_executable(airusb-server
    src/airusb-server.cpp
    src/server.cpp
)

target_link_libraries(airusb-server 
    airusb_common
    pthread
    ${LIBUSB_LIBRARIES}
)

# Client executable
add_executable(airusb-client
    src/airusb-client.cpp
    src/client.cpp
)

target_link_libraries(airusb-client 
    airusb_common
    pthread
    ${LIBUSB_LIBRARIES}
)

# Install targets
install(TARGETS airusb-server airusb-client
    RUNTIME DESTINATION bin
)

# Install systemd service files
install(FILES 
    scripts/airusb-server.service
    DESTINATION /etc/systemd/system
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
)

# Install udev rules for automatic device detection
install(FILES
    scripts/99-airusb.rules
    DESTINATION /etc/udev/rules.d
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
)

# Performance testing
add_executable(airusb-benchmark EXCLUDE_FROM_ALL
    tests/benchmark.cpp
)

target_link_libraries(airusb-benchmark airusb_common pthread)

# Enable testing
enable_testing()

add_test(NAME protocol_test 
         COMMAND airusb-benchmark --test-protocol)
add_test(NAME bandwidth_test 
         COMMAND airusb-benchmark --test-bandwidth)
add_test(NAME latency_test 
         COMMAND airusb-benchmark --test-latency)