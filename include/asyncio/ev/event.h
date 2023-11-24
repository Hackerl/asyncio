#ifndef ASYNCIO_EVENT_H
#define ASYNCIO_EVENT_H

#include <optional>
#include <event.h>
#include <cassert>
#include <asyncio/io.h>
#include <zero/async/coroutine.h>

namespace asyncio::ev {
    enum What : short {
        TIMEOUT = EV_TIMEOUT,
        READ = EV_READ,
        WRITE = EV_WRITE,
        CLOSED = EV_CLOSED
    };

    template<typename T>
    class Notifier {
    public:
        using Context = std::optional<zero::async::promise::Promise<T, std::error_code>>;

        explicit Notifier(event *e)
            : mEvent(
                e,
                [](event *ptr) {
                    delete static_cast<Context *>(event_get_callback_arg(ptr));
                    event_free(ptr);
                }
            ) {
        }

        Notifier(Notifier &&) = default;

        ~Notifier() {
            assert(!mEvent || !context().has_value());
        }

    protected:
        Context &context() {
            return *static_cast<Context *>(event_get_callback_arg(mEvent.get()));
        }

    public:
        bool cancel() {
            if (!pending())
                return false;

            event_del(mEvent.get());
            std::exchange(context(), std::nullopt)->reject(make_error_code(std::errc::operation_canceled));

            return true;
        }

        bool pending() {
            return context().has_value();
        }

    protected:
        std::unique_ptr<event, void (*)(event *)> mEvent;
    };

    class Event : public Notifier<short> {
    public:
        explicit Event(event *e);

        [[nodiscard]] FileDescriptor fd() const;
        void trigger(short events) const;

        zero::async::coroutine::Task<short, std::error_code>
        on(std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    };

    tl::expected<Event, std::error_code> makeEvent(FileDescriptor fd, short events);
}

#endif //ASYNCIO_EVENT_H
