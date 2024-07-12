#ifndef ASYNCIO_UV_H
#define ASYNCIO_UV_H

#include <uv.h>
#include <memory>
#include <optional>
#include <expected>
#include <functional>
#include <zero/error.h>

namespace asyncio::uv {
    DEFINE_ERROR_TRANSFORMER_EX(
        Error,
        "asyncio::uv",
        ([](const int value) -> std::string {
            std::array<char, 1024> buffer = {};
            uv_strerror_r(value, buffer.data(), buffer.size());
            return buffer.data();
        }),
        [](const int value) {
            std::optional<std::error_condition> condition;

            // TODO fill
            switch (value) {
            case UV_ECANCELED:
            case UV_EAI_CANCELED:
                condition = std::errc::operation_canceled;
                break;

            case UV_EAGAIN:
            case UV_EAI_AGAIN:
                condition = std::errc::resource_unavailable_try_again;
                break;

            case UV_ENOBUFS:
                condition = std::errc::no_buffer_space;
                break;

            case UV_EEXIST:
                condition = std::errc::file_exists;
                break;

            default:
                break;
            }

            return condition;
        }
    )

    template<typename F>
        requires std::is_same_v<std::invoke_result_t<F>, int>
    std::expected<std::invoke_result_t<F>, std::error_code> expected(F &&f) {
        const auto result = f();

        if (result < 0)
            return std::unexpected(make_error_code(static_cast<Error>(result)));

        return result;
    }

    template<typename T>
    class Handle {
    public:
        template<typename Deleter>
        explicit Handle(std::unique_ptr<T, Deleter> handle) : mHandle{
            handle.release(),
            [deleter = std::move(handle.get_deleter())](T *ptr) {
                ptr->data = new Deleter(deleter);
                uv_close(
                    reinterpret_cast<uv_handle_t *>(ptr),
                    [](uv_handle_t *h) {
                        const auto del = static_cast<Deleter *>(h->data);
                        (*del)(reinterpret_cast<T *>(h));
                        delete del;
                    }
                );
            }
        } {
        }

        T *raw() {
            return mHandle.get();
        }

        const T *raw() const {
            return mHandle.get();
        }

        T *operator->() {
            return mHandle.get();
        }

        const T *operator->() const {
            return mHandle.get();
        }

        void close() {
            mHandle.reset();
        }

    private:
        std::unique_ptr<T, std::function<void(T *)>> mHandle;
    };
}

DECLARE_ERROR_CODE(asyncio::uv::Error)

#endif //ASYNCIO_UV_H
