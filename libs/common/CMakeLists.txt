add_library(emergency_common
    src/event.cpp
    src/logger.cpp
)

target_include_directories(emergency_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
