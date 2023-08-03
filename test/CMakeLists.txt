find_package(Catch2 CONFIG REQUIRED)

add_executable(
        asyncio_test
        event_loop.cpp
        thread.cpp
        ev/pipe.cpp
        ev/event.cpp
        ev/buffer.cpp
        ev/signal.cpp
        ev/timer.cpp
        $<$<PLATFORM_ID:Windows>:main.cpp>
)

target_link_libraries(asyncio_test PRIVATE asyncio $<IF:$<PLATFORM_ID:Windows>,Catch2::Catch2,Catch2::Catch2WithMain>)