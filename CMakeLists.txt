cmake_minimum_required(VERSION 3.16)
project(MarketDataFeedHandler CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# ■■■ Compiler flags ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
add_compile_options(
 -O3
 -march=native
 -Wall
 -Wextra
 -Wpedantic
 -Wno-unused-result
 -pthread
)
include_directories(include)
# ■■■ Exchange Simulator (Server) ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
add_executable(exchange_simulator
 src/server/exchange_simulator.cpp
)
target_link_libraries(exchange_simulator pthread)
# ■■■ Feed Handler (Client) ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
add_executable(feed_handler
 src/client/feed_handler.cpp
 src/client/socket.cpp
 src/client/parser.cpp
 src/client/visualizer.cpp
)
target_link_libraries(feed_handler pthread)
# ■■■ Install ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
install(TARGETS exchange_simulator feed_handler
 RUNTIME DESTINATION bin)
