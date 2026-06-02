add_executable(emergency-siren-service
    src/main.cpp
)

target_link_libraries(emergency-siren-service
    PRIVATE
        emergency_common
)

install(TARGETS emergency-siren-service DESTINATION bin)
