cmake_minimum_required(VERSION 3.16)

project(distributed_low_latency_trading_platform LANGUAGES CXX)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LLT_ENABLE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

add_library(llt_core
    src/logging.cpp
    src/time.cpp
    src/risk.cpp
    src/order_book.cpp
    src/strategy.cpp
    src/oms.cpp
    src/exchange_gateway.cpp
    src/node.cpp
    src/threading.cpp
    src/tcp_transport.cpp
)

target_include_directories(llt_core PUBLIC include)

target_link_libraries(llt_core PUBLIC Threads::Threads)

target_compile_options(llt_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -O3
    -march=native
)

if (LLT_ENABLE_WARNINGS_AS_ERRORS)
    target_compile_options(llt_core PRIVATE -Werror)
endif()

add_executable(market_data_node apps/market_data_node.cpp)
target_link_libraries(market_data_node PRIVATE llt_core)

add_executable(strategy_node apps/strategy_node.cpp)
target_link_libraries(strategy_node PRIVATE llt_core)

add_executable(oms_node apps/oms_node.cpp)
target_link_libraries(oms_node PRIVATE llt_core)

add_executable(gateway_node apps/gateway_node.cpp)
target_link_libraries(gateway_node PRIVATE llt_core)

add_executable(simulator apps/simulator.cpp)
target_link_libraries(simulator PRIVATE llt_core)

add_executable(threaded_runtime apps/threaded_runtime.cpp)
target_link_libraries(threaded_runtime PRIVATE llt_core)

add_executable(tcp_transport_demo apps/tcp_transport_demo.cpp)
target_link_libraries(tcp_transport_demo PRIVATE llt_core)