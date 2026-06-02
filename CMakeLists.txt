cmake_minimum_required(VERSION 3.16)

project(emergency-notifyd VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_UBUS "Enable ubus integration" OFF)

add_subdirectory(libs/common)
add_subdirectory(apps/emergency-notifyd)
add_subdirectory(apps/emergency-notify-sim)
