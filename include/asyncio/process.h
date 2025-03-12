#ifndef ASYNCIO_PROCESS_H
#define ASYNCIO_PROCESS_H

#include <asyncio/pipe.h>
#include <zero/os/process.h>

namespace asyncio::process {
    using Process = zero::os::process::Process;
    using ExitStatus = zero::os::process::ExitStatus;
    using Output = zero::os::process::Output;

    class ChildProcess final : public Process {
    public:
        ChildProcess(Process process, std::array<std::optional<Pipe>, 3> stdio);

        std::optional<Pipe> &stdInput();
        std::optional<Pipe> &stdOutput();
        std::optional<Pipe> &stdError();

        task::Task<ExitStatus, std::error_code> wait();
        std::expected<std::optional<ExitStatus>, std::error_code> tryWait();

    private:
        std::array<std::optional<Pipe>, 3> mStdio;
    };

    class Command;

    class PseudoConsole {
    public:
        class Pipe final : public asyncio::Pipe {
        public:
            explicit Pipe(asyncio::Pipe pipe);

            task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        };

        PseudoConsole(zero::os::process::PseudoConsole pc, Pipe pipe);
        static std::expected<PseudoConsole, std::error_code> make(short rows, short columns);

#ifdef _WIN32
        void close();
#endif

        std::expected<void, std::error_code> resize(short rows, short columns);
        std::expected<ChildProcess, std::error_code> spawn(const Command &command);

        Pipe &pipe();

    private:
        zero::os::process::PseudoConsole mPseudoConsole;
        Pipe mPipe;
    };

    class Command {
    public:
        using StdioType = zero::os::process::Command::StdioType;

        explicit Command(std::filesystem::path path);

    private:
        [[nodiscard]] std::expected<ChildProcess, std::error_code>
        spawn(const std::array<StdioType, 3> &defaultTypes) const;

    public:
        template<typename Self>
        Self &&arg(this Self &&self, std::string arg) {
            self.mCommand.arg(std::move(arg));
            return std::forward<Self>(self);
        }

        template<typename Self>
        Self &&args(this Self &&self, std::vector<std::string> args) {
            self.mCommand.args(std::move(args));
            return std::forward<Self>(self);
        }

        template<typename Self>
        Self &&currentDirectory(this Self &&self, std::filesystem::path path) {
            self.mCommand.currentDirectory(std::move(path));
            return std::forward<Self>(self);
        }

        template<typename Self>
        Self &&env(this Self &&self, std::string key, std::string value) {
            self.mCommand.env(std::move(key), std::move(value));
            return std::forward<Self>(self);
        }

        template<typename Self>
        Self &&envs(this Self &&self, std::map<std::string, std::string> envs) {
            self.mCommand.envs(std::move(envs));
            return std::forward<Self>(self);
        }

        template<typename Self>
        Self &&clearEnv(this Self &&self) {
            self.mCommand.clearEnv();
            return std::forward<Self>(self);
        }

        template<typename Self>
        Self &&inheritedResource(this Self &&self, zero::os::Resource resource) {
            self.mCommand.inheritedResource(std::move(resource));
            return std::forward<Self>(self);
        }

        template<typename Self>
        Self &&inheritedResources(this Self &&self, std::vector<zero::os::Resource> resource) {
            self.mCommand.inheritedResources(std::move(resource));
            return std::forward<Self>(self);
        }

        template<typename Self>
        Self &&stdInput(this Self &&self, StdioType type) {
            self.mCommand.stdInput(type);
            return std::forward<Self>(self);
        }

        template<typename Self>
        Self &&stdOutput(this Self &&self, StdioType type) {
            self.mCommand.stdOutput(type);
            return std::forward<Self>(self);
        }

        template<typename Self>
        Self &&stdError(this Self &&self, StdioType type) {
            self.mCommand.stdError(type);
            return std::forward<Self>(self);
        }

        [[nodiscard]] const std::filesystem::path &program() const;
        [[nodiscard]] const std::vector<std::string> &args() const;
        [[nodiscard]] const std::optional<std::filesystem::path> &currentDirectory() const;
        [[nodiscard]] const std::map<std::string, std::optional<std::string>> &envs() const;
        [[nodiscard]] const std::vector<zero::os::Resource> &inheritedResources() const;

        [[nodiscard]] std::expected<ChildProcess, std::error_code> spawn() const;
        [[nodiscard]] task::Task<ExitStatus, std::error_code> status() const;
        [[nodiscard]] task::Task<Output, std::error_code> output() const;

    private:
        zero::os::process::Command mCommand;

        friend class PseudoConsole;
    };
}

#endif //ASYNCIO_PROCESS_H
