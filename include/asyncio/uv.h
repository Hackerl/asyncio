#ifndef ASYNCIO_UV_H
#define ASYNCIO_UV_H

#include <uv.h>
#include <array>
#include <memory>
#include <optional>
#include <expected>
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
            return std::unexpected(static_cast<Error>(result));

        return result;
    }

    template<typename T>
    class Handle {
    public:
        explicit Handle(std::unique_ptr<T> handle) : mHandle{
            handle.release(),
            [](T *ptr) {
                uv_close(
                    reinterpret_cast<uv_handle_t *>(ptr),
                    [](uv_handle_t *h) {
                        delete reinterpret_cast<T *>(h);
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
        std::unique_ptr<T, void(*)(T *)> mHandle;
    };
}

DECLARE_ERROR_CODE(asyncio::uv::Error)

#endif //ASYNCIO_UV_H
