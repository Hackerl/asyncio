module;

#include <asyncio/sync/condition.h>
#include <asyncio/sync/event.h>
#include <asyncio/sync/mutex.h>

export module asyncio:sync;

export namespace asyncio::sync {
    using asyncio::sync::Condition;

    using asyncio::sync::Event;

    using asyncio::sync::Mutex;
}
