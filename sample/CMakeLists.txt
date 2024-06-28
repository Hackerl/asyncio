add_executable(asyncio_tcp_client tcp/client.cpp)
target_link_libraries(asyncio_tcp_client PRIVATE asyncio-main)

add_executable(asyncio_tcp_server tcp/server.cpp)
target_link_libraries(asyncio_tcp_server PRIVATE asyncio-main)

add_executable(asyncio_tls_client tls/client.cpp)
target_link_libraries(asyncio_tls_client PRIVATE asyncio-main)

add_executable(asyncio_tls_server tls/server.cpp)
target_link_libraries(asyncio_tls_server PRIVATE asyncio-main)

add_executable(asyncio_http http/main.cpp)
target_link_libraries(asyncio_http PRIVATE asyncio-main)

add_executable(asyncio_ws ws/main.cpp)
target_link_libraries(asyncio_ws PRIVATE asyncio-main)
