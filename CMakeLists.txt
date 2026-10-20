cmake_minimum_required(VERSION 3.20)
project(hft_trade_lifecycle LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Lets clangd use the exact same include paths, language mode, and compiler
# flags as CMake instead of guessing from individual headers.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(hft_core
    exchange/exchange.cpp
    exchange/order_book.cpp
    market_data/local_book.cpp
    networking/market_data_codec.cpp
    networking/udp_multicast.cpp
    risk/risk_manager.cpp
    risk/volatility.cpp
    trader/market_maker.cpp
    trader/trading_engine.cpp
)
target_include_directories(hft_core PUBLIC .)
target_compile_options(hft_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(hft_demo simulation/main.cpp)
target_link_libraries(hft_demo PRIVATE hft_core)

add_executable(hft_tests tests/hft_tests.cpp)
target_link_libraries(hft_tests PRIVATE hft_core)

enable_testing()
add_test(NAME hft_tests COMMAND hft_tests)
