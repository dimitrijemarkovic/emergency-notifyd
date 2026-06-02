add_executable(emergency-notifyd
    src/main.cpp
)

target_link_libraries(emergency-notifyd
    PRIVATE
        emergency_common
)

install(TARGETS emergency-notifyd DESTINATION bin)
