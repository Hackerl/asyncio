#include <asyncio/process.h>
#include <asyncio/thread.h>
#include <asyncio/error.h>

#ifdef _WIN32
#include <zero/defer.h>
#include <zero/os/windows/error.h>
#else
#include <sys/wait.h>
#include <zero/os/unix/error.h>
#endif

asyncio::process::ChildProcess::ChildProcess(Process process, std::array<std::optional<Pipe>, 3> stdio)
    : Process{std::move(process)}, mStdio{std::move(stdio)} {
}

std::optional<asyncio::Pipe> &asyncio::process::ChildProcess::stdInput() {
    return mStdio[0];
}

std::optional<asyncio::Pipe> &asyncio::process::ChildProcess::stdOutput() {
    return mStdio[1];
}

std::optional<asyncio::Pipe> &asyncio::process::ChildProcess::stdError() {
    return mStdio[2];
}

// ReSharper disable once CppMemberFunctionMayBeConst
asyncio::task::Task<asyncio::process::ExitStatus, std::error_code> asyncio::process::ChildProcess::wait() {
#ifdef _WIN32
    const zero::os::Resource event{CreateEventA(nullptr, false, false, nullptr)};

    if (!event)
        throw co_await error::StacktraceError<std::system_error>::make(
            static_cast<int>(GetLastError()),
            std::system_category()
        );

    co_return co_await toThread(
        [&]() -> std::expected<ExitStatus, std::error_code> {
            const auto &impl = this->impl();
            const std::array handles{*impl.handle(), *event};

            const auto result = WaitForMultipleObjects(handles.size(), handles.data(), false, INFINITE);

            if (result >= WAIT_OBJECT_0 + handles.size())
                throw zero::error::StacktraceError<std::system_error>{
                    static_cast<int>(GetLastError()),
                    std::system_category()
                };

            if (result == WAIT_OBJECT_0 + 1)
                return std::unexpected{task::Error::Cancelled};

            return ExitStatus{zero::error::guard(impl.exitCode())};
        },
        [&](std::thread::native_handle_type) -> std::expected<void, std::error_code> {
            return zero::os::windows::expected([&] {
                return SetEvent(*event);
            });
        }
    );
#else
    co_return co_await toThread(
        [this]() -> std::expected<ExitStatus, std::error_code> {
            int s{};
            const auto pid = this->impl().pid();

            const auto id = zero::error::guard(zero::os::unix::ensure([&] {
                return waitpid(pid, &s, 0);
            }));
            assert(id == pid);

            return ExitStatus{s};
        }
    );
#endif
}

// ReSharper disable once CppMemberFunctionMayBeConst
std::optional<asyncio::process::ExitStatus> asyncio::process::ChildProcess::tryWait() {
#ifdef _WIN32
    using namespace std::chrono_literals;

    const auto &impl = this->impl();

    if (const auto result = impl.wait(0ms); !result) {
        if (const auto &error = result.error(); error != std::errc::timed_out)
            throw zero::error::StacktraceError<std::system_error>{error};

        return std::nullopt;
    }

    return ExitStatus{zero::error::guard(impl.exitCode())};
#else
    int s{};
    const auto pid = this->impl().pid();

    const auto id = zero::error::guard(zero::os::unix::expected([&] {
        return waitpid(pid, &s, WNOHANG);
    }));

    if (id == 0)
        return std::nullopt;

    return ExitStatus{s};
#endif
}

#ifdef _WIN32
asyncio::process::PseudoConsole::Pipe::Pipe(asyncio::Pipe reader, asyncio::Pipe writer)
    : mReader{std::move(reader)}, mWriter{std::move(writer)} {
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::process::PseudoConsole::Pipe::read(const std::span<std::byte> data) {
    return mReader.read(data);
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::process::PseudoConsole::Pipe::write(const std::span<const std::byte> data) {
    return mWriter.write(data);
}

asyncio::task::Task<void, std::error_code> asyncio::process::PseudoConsole::Pipe::close() {
    Z_CO_EXPECT(co_await mReader.close());
    co_return co_await mWriter.close();
}
#else
asyncio::process::PseudoConsole::Pipe::Pipe(asyncio::Pipe pipe) : asyncio::Pipe{std::move(pipe)} {
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::process::PseudoConsole::Pipe::read(const std::span<std::byte> data) {
    return asyncio::Pipe::read(data)
        .orElse([](const auto &ec) -> std::expected<std::size_t, std::error_code> {
            if (ec != std::errc::io_error)
                return std::unexpected{ec};

            return 0;
        });
}
#endif

asyncio::process::PseudoConsole::PseudoConsole(zero::os::process::PseudoConsole pc, Pipe pipe)
    : mPseudoConsole{std::move(pc)}, mPipe{std::move(pipe)} {
}

std::expected<asyncio::process::PseudoConsole, std::error_code>
asyncio::process::PseudoConsole::make(const short rows, const short columns) {
    auto pc = zero::os::process::PseudoConsole::make(rows, columns);
    Z_EXPECT(pc);

#ifdef _WIN32
    auto &[reader, writer] = pc->master();

    std::array fds{-1, -1};

    Z_DEFER(
        for (const auto &fd: fds) {
            if (fd == -1)
                continue;

            uv_fs_t request{};
            Z_DEFER(uv_fs_req_cleanup(&request));

            zero::error::guard(uv::expected([&] {
                return uv_fs_close(nullptr, &request, fd, nullptr);
            }));
        }
    );

    fds[0] = zero::error::guard(uv::expected([&] {
        return uv_open_osfhandle(reader.fd());
    }));
    std::ignore = reader.release();

    fds[1] = zero::error::guard(uv::expected([&] {
        return uv_open_osfhandle(writer.fd());
    }));
    std::ignore = writer.release();

    auto first = asyncio::Pipe::from(fds[0]);
    fds[0] = -1;

    auto second = asyncio::Pipe::from(fds[1]);
    fds[1] = -1;

    return PseudoConsole{*std::move(pc), {std::move(first), std::move(second)}};
#else
    auto &resource = pc->master();

    auto fd = zero::error::guard(uv::expected([&] {
        return uv_open_osfhandle(resource.fd());
    }));
    std::ignore = resource.release();

    Z_DEFER(
        if (fd == -1)
            return;

        uv_fs_t request{};
        Z_DEFER(uv_fs_req_cleanup(&request));

        zero::error::guard(uv::expected([&] {
            return uv_fs_close(nullptr, &request, fd, nullptr);
        }));
    );

    auto pipe = asyncio::Pipe::from(fd);
    fd = -1;

    return PseudoConsole{*std::move(pc), Pipe{std::move(pipe)}};
#endif
}

#ifdef _WIN32
void asyncio::process::PseudoConsole::close() {
    mPseudoConsole.close();
}
#endif

void asyncio::process::PseudoConsole::resize(const short rows, const short columns) {
    mPseudoConsole.resize(rows, columns);
}

std::expected<asyncio::process::ChildProcess, std::error_code>
asyncio::process::PseudoConsole::spawn(const Command &command) {
    return mPseudoConsole.spawn(command.mCommand).transform([](zero::os::process::ChildProcess &&process) {
        return ChildProcess{Process{std::move(process.impl())}, {}};
    });
}

asyncio::process::PseudoConsole::Pipe &asyncio::process::PseudoConsole::master() {
    return mPipe;
}

asyncio::process::Command::Command(std::filesystem::path path) : mCommand{std::move(path)} {
}

std::expected<asyncio::process::ChildProcess, std::error_code>
asyncio::process::Command::spawn(const std::array<StdioType, 3> &defaultTypes) const {
    auto child = mCommand.spawn(defaultTypes);
    Z_EXPECT(child);

    std::array<std::optional<Pipe>, 3> stdio;

    constexpr std::array memberPointers{
        &zero::os::process::ChildProcess::stdInput,
        &zero::os::process::ChildProcess::stdOutput,
        &zero::os::process::ChildProcess::stdError
    };

    for (int i{0}; i < 3; ++i) {
        auto &resource = (*child.*memberPointers[i])();

        if (!resource)
            continue;

        const auto fd = uv::expected([&] {
            return uv_open_osfhandle(resource->fd());
        });

        if (!fd) {
            zero::error::guard(
                child->kill().or_else([](const auto &ec) -> std::expected<void, std::error_code> {
#ifdef _WIN32
                    if (ec != std::errc::permission_denied)
#else
                    if (ec != std::errc::no_such_process)
#endif
                        return std::unexpected{ec};

                    return {};
                })
            );
            child->wait();
            throw zero::error::StacktraceError<std::system_error>{fd.error()};
        }

        std::ignore = resource->release();

        auto pipe = zero::error::capture([&] {
            return Pipe::from(*fd);
        });

        if (!pipe) {
            zero::error::guard(
                child->kill().or_else([](const auto &ec) -> std::expected<void, std::error_code> {
#ifdef _WIN32
                    if (ec != std::errc::permission_denied)
#else
                    if (ec != std::errc::no_such_process)
#endif
                        return std::unexpected{ec};

                    return {};
                })
            );
            child->wait();

            uv_fs_t request{};
            Z_DEFER(uv_fs_req_cleanup(&request));

            zero::error::guard(uv::expected([&] {
                return uv_fs_close(nullptr, &request, *fd, nullptr);
            }));

            std::rethrow_exception(pipe.error());
        }

        stdio[i].emplace(*std::move(pipe));
    }

    return ChildProcess{Process{std::move(child->impl())}, std::move(stdio)};
}

const std::filesystem::path &asyncio::process::Command::program() const {
    return mCommand.program();
}

const std::vector<std::string> &asyncio::process::Command::args() const {
    return mCommand.args();
}

const std::optional<std::filesystem::path> &asyncio::process::Command::currentDirectory() const {
    return mCommand.currentDirectory();
}

const std::map<std::string, std::optional<std::string>> &asyncio::process::Command::envs() const {
    return mCommand.envs();
}

const std::vector<zero::os::Resource> &asyncio::process::Command::inheritedResources() const {
    return mCommand.inheritedResources();
}

const std::vector<zero::os::Resource::Native> &asyncio::process::Command::inheritedNativeResources() const {
    return mCommand.inheritedNativeResources();
}

std::expected<asyncio::process::ChildProcess, std::error_code> asyncio::process::Command::spawn() const {
    return spawn({StdioType::Inherit, StdioType::Inherit, StdioType::Inherit});
}

asyncio::task::Task<asyncio::process::ExitStatus, std::error_code> asyncio::process::Command::status() const {
    auto child = spawn();
    Z_CO_EXPECT(child);
    co_return co_await task::Cancellable{
        child->wait(),
        [&] {
            return child->kill();
        }
    };
}

asyncio::task::Task<asyncio::process::Output, std::error_code> asyncio::process::Command::output() const {
    auto child = spawn({StdioType::Null, StdioType::Piped, StdioType::Piped});
    Z_CO_EXPECT(child);

    if (auto input = std::exchange(child->stdInput(), std::nullopt))
        co_await error::guard(input->close());

    auto result = co_await all(
        task::spawn([&]() -> task::Task<std::vector<std::byte>, std::error_code> {
            auto &out = child->stdOutput();

            if (!out)
                co_return {};

            co_return co_await out->readAll();
        }),
        task::spawn([&]() -> task::Task<std::vector<std::byte>, std::error_code> {
            auto &err = child->stdError();

            if (!err)
                co_return {};

            co_return co_await err->readAll();
        })
    );

    if (!result) {
        co_await task::lock;

        zero::error::guard(
            child->kill().or_else([](const auto &ec) -> std::expected<void, std::error_code> {
#ifdef _WIN32
                if (ec != std::errc::permission_denied)
#else
                if (ec != std::errc::no_such_process)
#endif
                    return std::unexpected{ec};

                return {};
            })
        );
        co_await error::guard(child->wait());

        co_await task::unlock;
        co_return std::unexpected{result.error()};
    }

    const auto status = co_await task::Cancellable{
        child->wait(),
        [&] {
            return child->kill();
        }
    };
    Z_CO_EXPECT(status);

    co_return Output{
        *status,
        std::move(result->at(0)),
        std::move(result->at(1))
    };
}
