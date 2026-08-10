if(ENABLE_UBUS)
    find_package(Threads REQUIRED)

    add_executable(emergency-mqtt-service
        src/mqtt_service.c
        src/mqtt_publisher.c
        src/mqtt_client.c
    )

    find_library(UBUS_LIBRARY ubus REQUIRED)
    find_library(UBOX_LIBRARY ubox REQUIRED)
    find_library(MOSQUITTO_LIBRARY mosquitto REQUIRED)

    target_link_libraries(emergency-mqtt-service
        PRIVATE
            ${UBUS_LIBRARY}
            ${UBOX_LIBRARY}
            ${MOSQUITTO_LIBRARY}
            Threads::Threads
            emergency_log
    )
else()
    add_executable(emergency-mqtt-service
        src/main.cpp
    )

    target_link_libraries(emergency-mqtt-service
        PRIVATE
            emergency_common
    )
endif()

install(TARGETS emergency-mqtt-service DESTINATION bin)
