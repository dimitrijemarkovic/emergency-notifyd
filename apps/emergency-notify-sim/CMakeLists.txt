add_executable(emergency-notify-sim
    src/sim_ubus.c
)

find_library(UBUS_LIBRARY ubus REQUIRED)
find_library(UBOX_LIBRARY ubox REQUIRED)

target_link_libraries(emergency-notify-sim
    PRIVATE
        ${UBUS_LIBRARY}
        ${UBOX_LIBRARY}
)

install(TARGETS emergency-notify-sim DESTINATION bin)