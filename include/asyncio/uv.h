#ifndef ASYNCIO_UV_H
#define ASYNCIO_UV_H

#include <uv.h>
#include <memory>
#include <optional>
#include <expected>
#include <functional>
#include <zero/error.h>
#include <zero/expect.h>

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
        explicit Handle(std::unique_ptr<T, Deleter> handle) : mHandle(std::move(handle)) {
        }

        Handle(Handle &&rhs) noexcept : mHandle(std::move(rhs.mHandle)) {
        }

        Handle &operator=(Handle &&rhs) noexcept {
            mHandle = std::move(rhs.mHandle);
            return *this;
        }

        ~Handle() {
            if (!mHandle)
                return;

            const auto handle = mHandle.release();
            handle->data = new std::function<void(T *)>(std::move(mHandle.get_deleter()));

            uv_close(
                reinterpret_cast<uv_handle_t *>(handle),
                [](uv_handle_t *h) {
                    const auto del = static_cast<std::function<void(T *)> *>(h->data);
                    (*del)(reinterpret_cast<T *>(h));
                    delete del;
                }
            );
        }

        [[nodiscard]] std::expected<uv_os_fd_t, std::error_code> fd() const {
            uv_os_fd_t fd;

            EXPECT(expected([&] {
                return uv_fileno(rawHandle(), &fd);
            }));

            return fd;
        }

        T *raw() {
            return mHandle.get();
        }

        [[nodiscard]] const T *raw() const {
            return mHandle.get();
        }

        uv_handle_t *rawHandle() {
            return reinterpret_cast<uv_handle_t *>(mHandle.get());
        }

        [[nodiscard]] const uv_handle_t *rawHandle() const {
            return reinterpret_cast<const uv_handle_t *>(mHandle.get());
        }

        T *operator->() {
            return mHandle.get();
        }

        [[nodiscard]] const T *operator->() const {
            return mHandle.get();
        }

        std::unique_ptr<T, std::function<void(T *)>> release() {
            return std::move(mHandle);
        }

    private:
        std::unique_ptr<T, std::function<void(T *)>> mHandle;
    };
}

DECLARE_ERROR_CODE(asyncio::uv::Error)

#endif //ASYNCIO_UV_H
