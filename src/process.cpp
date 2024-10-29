#include <asyncio/process.h>
#include <asyncio/thread.h>

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
    const auto event = CreateEventA(nullptr, false, false, nullptr);

    if (!event)
        co_return std::unexpected{std::error_code{static_cast<int>(GetLastError()), std::system_category()}};

    DEFER(CloseHandle(event));

    co_return co_await toThread(
        [&]() -> std::expected<ExitStatus, std::error_code> {
            const auto &impl = this->impl();
            const std::array handles{impl.handle(), event};

            const auto result = WaitForMultipleObjects(handles.size(), handles.data(), false, INFINITE);

            if (result >= WAIT_OBJECT_0 + handles.size())
                return std::unexpected{std::error_code{static_cast<int>(GetLastError()), std::system_category()}};

            if (result == WAIT_OBJECT_0 + 1)
                return std::unexpected{make_error_code(std::errc::operation_canceled)};

            return impl.exitCode().transform([](const auto &code) {
                return ExitStatus{code};
            });
        },
        [&](std::thread::native_handle_type) -> std::expected<void, std::error_code> {
            return zero::os::windows::expected([&] {
                return SetEvent(event);
            });
        }
    );
#else
    co_return co_await toThread(
        [this]() -> std::expected<ExitStatus, std::error_code> {
            int s{};

            const auto pid = this->impl().pid();
            const auto id = zero::os::unix::ensure([&] {
                return waitpid(pid, &s, 0);
            });
            EXPECT(id);
            assert(*id == pid);

            return ExitStatus{s};
        }
    );
#endif
}

// ReSharper disable once CppMemberFunctionMayBeConst
std::expected<std::optional<asyncio::process::ExitStatus>, std::error_code> asyncio::process::ChildProcess::tryWait() {
#ifdef _WIN32
    using namespace std::chrono_literals;

    const auto &impl = this->impl();

    if (const auto result = impl.wait(0ms); !result) {
        if (result.error() == std::errc::timed_out)
            return std::nullopt;

        return std::unexpected{result.error()};
    }

    return impl.exitCode().transform([](const auto &code) {
        return ExitStatus{code};
    });
#else
    int s{};

    const auto pid = this->impl().pid();
    const auto id = zero::os::unix::expected([&] {
        return waitpid(pid, &s, WNOHANG);
    });
    EXPECT(id);

    if (*id == 0)
        return std::nullopt;

    return ExitStatus{s};
#endif
}

asyncio::process::PseudoConsole::Pipe::Pipe(asyncio::Pipe pipe) : asyncio::Pipe{std::move(pipe)} {
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::process::PseudoConsole::Pipe::read(const std::span<std::byte> data) {
#ifdef __linux__
    co_return co_await asyncio::Pipe::read(data)
        .orElse([](const auto &ec) -> std::expected<std::size_t, std::error_code> {
            if (ec != std::errc::io_error)
                return std::unexpected{ec};

            return 0;
        });
#else
    co_return co_await asyncio::Pipe::read(data);
#endif
}

asyncio::process::PseudoConsole::PseudoConsole(zero::os::process::PseudoConsole pc, Pipe pipe)
    : mPseudoConsole{std::move(pc)}, mPipe{std::move(pipe)} {
}

std::expected<asyncio::process::PseudoConsole, std::error_code>
asyncio::process::PseudoConsole::make(const short rows, const short columns) {
    auto pc = zero::os::process::PseudoConsole::make(rows, columns);
    EXPECT(pc);

    auto &file = pc->file();

    const auto fd = uv::expected([&] {
        return uv_open_osfhandle(file);
    });
    EXPECT(fd);

#ifdef _WIN32
    file = nullptr;
#else
    file = -1;
#endif

    auto pipe = asyncio::Pipe::from(*fd);

    if (!pipe) {
        uv_fs_t request{};
        uv_fs_close(nullptr, &request, *fd, nullptr);
        uv_fs_req_cleanup(&request);
        return std::unexpected{pipe.error()};
    }

    if (!pipe) {
        uv_fs_t request{};
        uv_fs_close(nullptr, &request, *fd, nullptr);
        uv_fs_req_cleanup(&request);
        return std::unexpected{pipe.error()};
    }

    return PseudoConsole{*std::move(pc), Pipe{*std::move(pipe)}};
}

#ifdef _WIN32
void asyncio::process::PseudoConsole::close() {
    mPseudoConsole.close();
}
#endif

std::expected<void, std::error_code> asyncio::process::PseudoConsole::resize(const short rows, const short columns) {
    return mPseudoConsole.resize(rows, columns);
}

std::expected<asyncio::process::ChildProcess, std::error_code>
asyncio::process::PseudoConsole::spawn(const Command &command) {
    return mPseudoConsole.spawn(command.mCommand).transform([](auto process) {
        return ChildProcess{zero::os::process::Process{std::move(process.impl())}, {}};
    });
}

asyncio::process::PseudoConsole::Pipe &asyncio::process::PseudoConsole::pipe() {
    return mPipe;
}

asyncio::process::Command::Command(std::filesystem::path path) : mCommand{std::move(path)} {
}

std::expected<asyncio::process::ChildProcess, std::error_code>
asyncio::process::Command::spawn(const std::array<StdioType, 3> &defaultTypes) const {
    auto child = mCommand.spawn(defaultTypes);
    EXPECT(child);

    std::array<std::optional<Pipe>, 3> stdio;

    constexpr std::array memberPointers{
        &zero::os::process::ChildProcess::stdInput,
        &zero::os::process::ChildProcess::stdOutput,
        &zero::os::process::ChildProcess::stdError
    };

    for (int i{0}; i < 3; ++i) {
        auto &fd = (*child.*memberPointers[i])();

        if (!fd)
            continue;

        const auto file = uv::expected([&] {
            return uv_open_osfhandle(*fd);
        });

        if (!file) {
            std::ignore = child->kill();
            std::ignore = child->wait();
            return std::unexpected{file.error()};
        }

        fd.reset();

        auto pipe = Pipe::from(*file);

        if (!pipe) {
            std::ignore = child->kill();
            std::ignore = child->wait();

            uv_fs_t request{};
            uv_fs_close(nullptr, &request, *file, nullptr);
            uv_fs_req_cleanup(&request);

            return std::unexpected{pipe.error()};
        }

        stdio[i].emplace(*std::move(pipe));
    }

    return ChildProcess{zero::os::process::Process{std::move(child->impl())}, std::move(stdio)};
}

asyncio::process::Command &asyncio::process::Command::arg(std::string arg) {
    mCommand.arg(std::move(arg));
    return *this;
}

asyncio::process::Command &asyncio::process::Command::args(std::vector<std::string> args) {
    mCommand.args(std::move(args));
    return *this;
}

asyncio::process::Command &asyncio::process::Command::currentDirectory(std::filesystem::path path) {
    mCommand.currentDirectory(std::move(path));
    return *this;
}

asyncio::process::Command &asyncio::process::Command::env(std::string key, std::string value) {
    mCommand.env(std::move(key), std::move(value));
    return *this;
}

asyncio::process::Command &asyncio::process::Command::envs(std::map<std::string, std::string> envs) {
    mCommand.envs(std::move(envs));
    return *this;
}

asyncio::process::Command &asyncio::process::Command::clearEnv() {
    mCommand.clearEnv();
    return *this;
}

asyncio::process::Command &asyncio::process::Command::removeEnv(const std::string &key) {
    mCommand.removeEnv(key);
    return *this;
}

asyncio::process::Command &asyncio::process::Command::stdInput(const StdioType type) {
    mCommand.stdInput(type);
    return *this;
}

asyncio::process::Command &asyncio::process::Command::stdOutput(const StdioType type) {
    mCommand.stdOutput(type);
    return *this;
}

asyncio::process::Command &asyncio::process::Command::stdError(const StdioType type) {
    mCommand.stdError(type);
    return *this;
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

std::expected<asyncio::process::ChildProcess, std::error_code> asyncio::process::Command::spawn() const {
    return spawn({StdioType::INHERIT, StdioType::INHERIT, StdioType::INHERIT});
}

asyncio::task::Task<asyncio::process::ExitStatus, std::error_code> asyncio::process::Command::status() const {
    auto child = spawn();
    CO_EXPECT(child);
    co_return co_await child->wait();
}

asyncio::task::Task<asyncio::process::Output, std::error_code> asyncio::process::Command::output() const {
    auto child = spawn({StdioType::NUL, StdioType::PIPED, StdioType::PIPED});
    CO_EXPECT(child);

    if (auto input = std::exchange(child->stdInput(), std::nullopt))
        co_await input->close();

    auto result = co_await all(
        [&]() -> task::Task<std::vector<std::byte>, std::error_code> {
            auto &out = child->stdOutput();

            if (!out)
                co_return {};

            co_return co_await out->readAll();
        }(),
        [&]() -> task::Task<std::vector<std::byte>, std::error_code> {
            auto &err = child->stdError();

            if (!err)
                co_return {};

            co_return co_await err->readAll();
        }()
    );

    if (!result) {
        std::ignore = child->kill();
        co_await child->wait();
        co_return std::unexpected{result.error()};
    }

    const auto status = co_await child->wait();
    CO_EXPECT(status);

    co_return Output{
        *status,
        std::move(result->at(0)),
        std::move(result->at(1))
    };
}
