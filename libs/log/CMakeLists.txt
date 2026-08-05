add_library(emergency_log
    src/log_core.c
    src/log_backend_stdout.c
)

target_include_directories(emergency_log
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(ENABLE_ZLOG)
    target_sources(emergency_log
        PRIVATE
            src/log_backend_zlog.c
    )

    find_library(ZLOG_LIBRARY zlog REQUIRED)

    target_compile_definitions(emergency_log
        PRIVATE
            ENABLE_ZLOG=1
    )

    target_link_libraries(emergency_log
        PRIVATE
            ${ZLOG_LIBRARY}
    )
else()
    target_compile_definitions(emergency_log
        PRIVATE
            ENABLE_ZLOG=0
    )
endif()
