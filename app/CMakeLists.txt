cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(link_logger LANGUAGES C)
target_include_directories(app PRIVATE include)
target_sources(app PRIVATE
  src/main.c src/app.c src/state.c src/sensors.c src/record.c
  src/timekeeping.c src/logging.c src/config.c src/com.c src/shell.c src/heartbeat.c)
target_compile_options(app PRIVATE -Wall -Wextra -Werror -Wformat=2)
