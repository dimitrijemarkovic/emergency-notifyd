add_executable(emergency-notify-sim
    src/main.cpp
)

target_link_libraries(emergency-notify-sim
    PRIVATE
        emergency_common
)

install(TARGETS emergency-notify-sim DESTINATION bin)
