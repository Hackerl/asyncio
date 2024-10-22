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
            std::array<char, 1024> buffer{};
            uv_strerror_r(value, buffer.data(), buffer.size());
            return buffer.data();
        }),
        [](const int value) -> std::optional<std::error_condition> {
            switch (value) {
            case UV_E2BIG:
                return std::errc::argument_list_too_long;

            case UV_EACCES:
                return std::errc::permission_denied;

            case UV_EADDRINUSE:
                return std::errc::address_in_use;

            case UV_EADDRNOTAVAIL:
                return std::errc::address_not_available;

            case UV_EAFNOSUPPORT:
                return std::errc::address_family_not_supported;

            case UV_EAGAIN:
            case UV_EAI_AGAIN:
                return std::errc::resource_unavailable_try_again;

            case UV_EAI_ADDRFAMILY:
            case UV_EAI_FAMILY:
                return std::errc::address_family_not_supported;

            case UV_EAI_BADFLAGS:
            case UV_EAI_BADHINTS:
                return std::errc::invalid_argument;

            case UV_EAI_CANCELED:
            case UV_ECANCELED:
                return std::errc::operation_canceled;

            case UV_EAI_MEMORY:
            case UV_ENOMEM:
                return std::errc::not_enough_memory;

            case UV_EAI_OVERFLOW:
            case UV_EOVERFLOW:
                return std::errc::value_too_large;

            case UV_EAI_PROTOCOL:
            case UV_EPROTONOSUPPORT:
                return std::errc::protocol_not_supported;

            case UV_EALREADY:
                return std::errc::connection_already_in_progress;

            case UV_EBADF:
                return std::errc::bad_file_descriptor;

            case UV_EBUSY:
                return std::errc::device_or_resource_busy;

            case UV_ECHARSET:
                return std::errc::illegal_byte_sequence;

            case UV_ECONNABORTED:
                return std::errc::connection_aborted;

            case UV_ECONNREFUSED:
                return std::errc::connection_refused;

            case UV_ECONNRESET:
                return std::errc::connection_reset;

            case UV_EDESTADDRREQ:
                return std::errc::destination_address_required;

            case UV_EEXIST:
                return std::errc::file_exists;

            case UV_EFAULT:
                return std::errc::bad_address;

            case UV_EFBIG:
                return std::errc::file_too_large;

            case UV_EHOSTUNREACH:
                return std::errc::host_unreachable;

            case UV_EINTR:
                return std::errc::interrupted;

            case UV_EINVAL:
                return std::errc::invalid_argument;

            case UV_EIO:
                return std::errc::io_error;

            case UV_EISCONN:
                return std::errc::already_connected;

            case UV_EISDIR:
                return std::errc::is_a_directory;

            case UV_ELOOP:
                return std::errc::too_many_symbolic_link_levels;

            case UV_EMFILE:
                return std::errc::too_many_files_open;

            case UV_EMSGSIZE:
                return std::errc::message_size;

            case UV_ENAMETOOLONG:
                return std::errc::filename_too_long;

            case UV_ENETDOWN:
                return std::errc::network_down;

            case UV_ENETUNREACH:
                return std::errc::network_unreachable;

            case UV_ENFILE:
                return std::errc::too_many_files_open_in_system;

            case UV_ENOBUFS:
                return std::errc::no_buffer_space;

            case UV_ENODEV:
                return std::errc::no_such_device;

            case UV_ENOENT:
                return std::errc::no_such_file_or_directory;

            case UV_ENONET:
                return std::errc::network_unreachable;

            case UV_ENOPROTOOPT:
                return std::errc::no_protocol_option;

            case UV_ENOSPC:
                return std::errc::no_space_on_device;

            case UV_ENOSYS:
                return std::errc::function_not_supported;

            case UV_ENOTCONN:
                return std::errc::not_connected;

            case UV_ENOTDIR:
                return std::errc::not_a_directory;

            case UV_ENOTEMPTY:
                return std::errc::directory_not_empty;

            case UV_ENOTSOCK:
                return std::errc::not_a_socket;

            case UV_ENOTSUP:
                return std::errc::operation_not_supported;

            case UV_EPERM:
                return std::errc::operation_not_permitted;

            case UV_ESHUTDOWN:
            case UV_EPIPE:
                return std::errc::broken_pipe;

            case UV_EPROTO:
                return std::errc::protocol_error;

            case UV_EPROTOTYPE:
                return std::errc::wrong_protocol_type;

            case UV_ERANGE:
                return std::errc::result_out_of_range;

            case UV_EROFS:
                return std::errc::read_only_file_system;

            case UV_ESPIPE:
                return std::errc::invalid_seek;

            case UV_ESRCH:
                return std::errc::no_such_process;

            case UV_ETIMEDOUT:
                return std::errc::timed_out;

            case UV_ETXTBSY:
                return std::errc::text_file_busy;

            case UV_EXDEV:
                return std::errc::cross_device_link;

            case UV_EILSEQ:
                return std::errc::illegal_byte_sequence;

            case UV_ENXIO:
                return std::errc::no_such_device_or_address;

            case UV_EMLINK:
                return std::errc::too_many_links;

            case UV_ENOTTY:
                return std::errc::inappropriate_io_control_operation;

            default:
                return std::nullopt;
            }
        }
    )

    template<typename F>
        requires std::is_same_v<std::invoke_result_t<F>, int>
    std::expected<std::invoke_result_t<F>, std::error_code> expected(F &&f) {
        const auto result = f();

        if (result < 0)
            return std::unexpected{make_error_code(static_cast<Error>(result))};

        return result;
    }

    template<typename T>
    class Handle {
    public:
        template<typename Deleter>
        explicit Handle(std::unique_ptr<T, Deleter> handle) : mHandle{std::move(handle)} {
        }

        Handle(Handle &&rhs) noexcept : mHandle{std::move(rhs.mHandle)} {
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
                [](auto *h) {
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
