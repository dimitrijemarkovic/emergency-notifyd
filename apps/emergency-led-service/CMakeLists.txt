if(ENABLE_UBUS)
    find_package(Threads REQUIRED)

    add_executable(emergency-led-service
        src/led_service.c
        src/led_controller.c
        src/led_hw.c
    )

    find_library(UBUS_LIBRARY ubus REQUIRED)
    find_library(UBOX_LIBRARY ubox REQUIRED)

    target_link_libraries(emergency-led-service
        PRIVATE
            ${UBUS_LIBRARY}
            ${UBOX_LIBRARY}
            Threads::Threads
            emergency_log
    )
else()
    add_executable(emergency-led-service
        src/main.cpp
    )

    target_link_libraries(emergency-led-service
        PRIVATE
            emergency_common
    )
endif()

install(TARGETS emergency-led-service DESTINATION bin)
