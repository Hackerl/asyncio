add_executable(asyncio_tcp_client tcp/client.cpp)
target_link_libraries(asyncio_tcp_client PRIVATE asyncio)

add_executable(asyncio_tcp_server tcp/server.cpp)
target_link_libraries(asyncio_tcp_server PRIVATE asyncio)

if (NOT ASYNCIO_DISABLE_SSL)
    add_executable(asyncio_ssl_client ssl/client.cpp)
    target_link_libraries(asyncio_ssl_client PRIVATE asyncio)

    add_executable(asyncio_ssl_server ssl/server.cpp)
    target_link_libraries(asyncio_ssl_server PRIVATE asyncio)
endif ()

add_executable(asyncio_http http/main.cpp)
target_link_libraries(asyncio_http PRIVATE asyncio)