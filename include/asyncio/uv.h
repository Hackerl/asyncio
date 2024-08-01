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

            switch (value) {
            case UV_E2BIG:
                condition = std::errc::argument_list_too_long;
                break;

            case UV_EACCES:
                condition = std::errc::permission_denied;
                break;

            case UV_EADDRINUSE:
                condition = std::errc::address_in_use;
                break;

            case UV_EADDRNOTAVAIL:
                condition = std::errc::address_not_available;
                break;

            case UV_EAFNOSUPPORT:
                condition = std::errc::address_family_not_supported;
                break;

            case UV_EAGAIN:
            case UV_EAI_AGAIN:
                condition = std::errc::resource_unavailable_try_again;
                break;

            case UV_EAI_ADDRFAMILY:
            case UV_EAI_FAMILY:
                condition = std::errc::address_family_not_supported;
                break;

            case UV_EAI_BADFLAGS:
            case UV_EAI_BADHINTS:
                condition = std::errc::invalid_argument;
                break;

            case UV_EAI_CANCELED:
            case UV_ECANCELED:
                condition = std::errc::operation_canceled;
                break;

            case UV_EAI_MEMORY:
            case UV_ENOMEM:
                condition = std::errc::not_enough_memory;
                break;

            case UV_EAI_OVERFLOW:
            case UV_EOVERFLOW:
                condition = std::errc::value_too_large;
                break;

            case UV_EAI_PROTOCOL:
            case UV_EPROTONOSUPPORT:
                condition = std::errc::protocol_not_supported;
                break;

            case UV_EALREADY:
                condition = std::errc::connection_already_in_progress;
                break;

            case UV_EBADF:
                condition = std::errc::bad_file_descriptor;
                break;

            case UV_EBUSY:
                condition = std::errc::device_or_resource_busy;
                break;

            case UV_ECHARSET:
                condition = std::errc::illegal_byte_sequence;
                break;

            case UV_ECONNABORTED:
                condition = std::errc::connection_aborted;
                break;

            case UV_ECONNREFUSED:
                condition = std::errc::connection_refused;
                break;

            case UV_ECONNRESET:
                condition = std::errc::connection_reset;
                break;

            case UV_EDESTADDRREQ:
                condition = std::errc::destination_address_required;
                break;

            case UV_EEXIST:
                condition = std::errc::file_exists;
                break;

            case UV_EFAULT:
                condition = std::errc::bad_address;
                break;

            case UV_EFBIG:
                condition = std::errc::file_too_large;
                break;

            case UV_EHOSTUNREACH:
                condition = std::errc::host_unreachable;
                break;

            case UV_EINTR:
                condition = std::errc::interrupted;
                break;

            case UV_EINVAL:
                condition = std::errc::invalid_argument;
                break;

            case UV_EIO:
                condition = std::errc::io_error;
                break;

            case UV_EISCONN:
                condition = std::errc::already_connected;
                break;

            case UV_EISDIR:
                condition = std::errc::is_a_directory;
                break;

            case UV_ELOOP:
                condition = std::errc::too_many_symbolic_link_levels;
                break;

            case UV_EMFILE:
                condition = std::errc::too_many_files_open;
                break;

            case UV_EMSGSIZE:
                condition = std::errc::message_size;
                break;

            case UV_ENAMETOOLONG:
                condition = std::errc::filename_too_long;
                break;

            case UV_ENETDOWN:
                condition = std::errc::network_down;
                break;

            case UV_ENETUNREACH:
                condition = std::errc::network_unreachable;
                break;

            case UV_ENFILE:
                condition = std::errc::too_many_files_open_in_system;
                break;

            case UV_ENOBUFS:
                condition = std::errc::no_buffer_space;
                break;

            case UV_ENODEV:
                condition = std::errc::no_such_device;
                break;

            case UV_ENOENT:
                condition = std::errc::no_such_file_or_directory;
                break;

            case UV_ENONET:
                condition = std::errc::network_unreachable;
                break;

            case UV_ENOPROTOOPT:
                condition = std::errc::no_protocol_option;
                break;

            case UV_ENOSPC:
                condition = std::errc::no_space_on_device;
                break;

            case UV_ENOSYS:
                condition = std::errc::function_not_supported;
                break;

            case UV_ENOTCONN:
                condition = std::errc::not_connected;
                break;

            case UV_ENOTDIR:
                condition = std::errc::not_a_directory;
                break;

            case UV_ENOTEMPTY:
                condition = std::errc::directory_not_empty;
                break;

            case UV_ENOTSOCK:
                condition = std::errc::not_a_socket;
                break;

            case UV_ENOTSUP:
                condition = std::errc::operation_not_supported;
                break;

            case UV_EPERM:
                condition = std::errc::operation_not_permitted;
                break;

            case UV_ESHUTDOWN:
            case UV_EPIPE:
                condition = std::errc::broken_pipe;
                break;

            case UV_EPROTO:
                condition = std::errc::protocol_error;
                break;

            case UV_EPROTOTYPE:
                condition = std::errc::wrong_protocol_type;
                break;

            case UV_ERANGE:
                condition = std::errc::result_out_of_range;
                break;

            case UV_EROFS:
                condition = std::errc::read_only_file_system;
                break;

            case UV_ESPIPE:
                condition = std::errc::invalid_seek;
                break;

            case UV_ESRCH:
                condition = std::errc::no_such_process;
                break;

            case UV_ETIMEDOUT:
                condition = std::errc::timed_out;
                break;

            case UV_ETXTBSY:
                condition = std::errc::text_file_busy;
                break;

            case UV_EXDEV:
                condition = std::errc::cross_device_link;
                break;

            case UV_EILSEQ:
                condition = std::errc::illegal_byte_sequence;
                break;

            case UV_ENXIO:
                condition = std::errc::no_such_device_or_address;
                break;

            case UV_EMLINK:
                condition = std::errc::too_many_links;
                break;

            case UV_ENOTTY:
                condition = std::errc::inappropriate_io_control_operation;
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

            close();
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

        [[nodiscard]] explicit operator bool() const {
            return mHandle.operator bool();
        }

        std::unique_ptr<T, std::function<void(T *)>> release() {
            return std::move(mHandle);
        }

        void close() {
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

    private:
        std::unique_ptr<T, std::function<void(T *)>> mHandle;
    };
}

DECLARE_ERROR_CODE(asyncio::uv::Error)

#endif //ASYNCIO_UV_H
