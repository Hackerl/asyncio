find_package(Catch2 CONFIG REQUIRED)

add_executable(
        asyncio_test
        io.cpp
        event_loop.cpp
        thread.cpp
        channel.cpp
        ev/pipe.cpp
        ev/event.cpp
        ev/buffer.cpp
        ev/signal.cpp
        ev/timer.cpp
        net/net.cpp
        net/dns.cpp
        net/dgram.cpp
        net/stream.cpp
        $<$<NOT:$<BOOL:${ASYNCIO_DISABLE_SSL}>>:net/ssl.cpp>
        $<$<PLATFORM_ID:Windows>:main.cpp>
)

target_link_libraries(asyncio_test PRIVATE asyncio $<IF:$<PLATFORM_ID:Windows>,Catch2::Catch2,Catch2::Catch2WithMain>)

include(Catch)
catch_discover_tests(asyncio_test)