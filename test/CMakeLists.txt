find_package(Catch2 CONFIG REQUIRED)

add_executable(
        asyncio_test
        io.cpp
        binary.cpp
        future.cpp
        event_loop.cpp
        thread.cpp
        channel.cpp
        sync/mutex.cpp
        sync/event.cpp
        sync/condition.cpp
        ev/pipe.cpp
        ev/event.cpp
        ev/buffer.cpp
        ev/signal.cpp
        ev/timer.cpp
        fs/file.cpp
        fs/pipe.cpp
        net/net.cpp
        net/dns.cpp
        net/ssl.cpp
        net/dgram.cpp
        net/stream.cpp
        http/url.cpp
        http/request.cpp
        $<$<PLATFORM_ID:Linux,Android>:fs/aio.cpp>
        $<$<PLATFORM_ID:Linux,Darwin>:fs/posix.cpp>
        $<$<PLATFORM_ID:Windows>:main.cpp fs/iocp.cpp>
)

target_link_libraries(asyncio_test PRIVATE asyncio $<IF:$<PLATFORM_ID:Windows>,Catch2::Catch2,Catch2::Catch2WithMain>)

include(Catch)
catch_discover_tests(asyncio_test)