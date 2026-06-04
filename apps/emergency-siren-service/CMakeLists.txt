if(ENABLE_UBUS)
    find_package(Threads REQUIRED)

    add_executable(emergency-siren-service
        src/siren_service.c
        src/siren_controller.c
        src/siren_hw.c
    )

    find_library(UBUS_LIBRARY ubus REQUIRED)
    find_library(UBOX_LIBRARY ubox REQUIRED)

    target_link_libraries(emergency-siren-service
        PRIVATE
            ${UBUS_LIBRARY}
            ${UBOX_LIBRARY}
            Threads::Threads
    )
else()
    add_executable(emergency-siren-service
        src/main.cpp
    )

    target_link_libraries(emergency-siren-service
        PRIVATE
            emergency_common
    )
endif()

install(TARGETS emergency-siren-service DESTINATION bin)