add_executable(emergency-notifyd
    src/main.cpp
)

if(ENABLE_UBUS)
    target_sources(emergency-notifyd
        PRIVATE
            src/notifyd_ubus.c
    )

    find_library(UBUS_LIBRARY ubus REQUIRED)
    find_library(UBOX_LIBRARY ubox REQUIRED)
    find_library(BLOBMSG_JSON_LIBRARY blobmsg_json REQUIRED)

    target_compile_definitions(emergency-notifyd
        PRIVATE
            ENABLE_UBUS=1
    )

    target_link_libraries(emergency-notifyd
        PRIVATE
            ${UBUS_LIBRARY}
            ${UBOX_LIBRARY}
            ${BLOBMSG_JSON_LIBRARY}
    )
endif()

target_link_libraries(emergency-notifyd
    PRIVATE
        emergency_common
)

install(TARGETS emergency-notifyd DESTINATION bin)
