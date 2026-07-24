#ifndef ASYNCIO_PROCESS_H
#define ASYNCIO_PROCESS_H

#include "pipe.h"
#include <zero/os/process.h>

namespace asyncio::process {
    using zero::os::process::Process;
    using zero::os::process::ExitStatus;
    using zero::os::process::Output;

    class ChildProcess final : public Process {
    public:
        ChildProcess(Process process, std::array<std::optional<Pipe>, 3> stdio);

        std::optional<Pipe> &stdInput();
        std::optional<Pipe> &stdOutput();
        std::optional<Pipe> &stdError();

        task::Task<ExitStatus, std::error_code> wait();
        std::optional<ExitStatus> tryWait();

    private:
        std::array<std::optional<Pipe>, 3> mStdio;
    };

    class Command;

    class PseudoConsole {
    public:
#ifdef _WIN32
        class Pipe final : public IReader, public IWriter, public ICloseable {
        public:
            Pipe(asyncio::Pipe reader, asyncio::Pipe writer);

            task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
            task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;
            task::Task<void, std::error_code> close() override;

        private:
            asyncio::Pipe mReader;
            asyncio::Pipe mWriter;
        };
#else
        class Pipe final : public asyncio::Pipe {
        public:
            explicit Pipe(asyncio::Pipe pipe);

            task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        };
#endif

        PseudoConsole(zero::os::process::PseudoConsole pc, Pipe pipe);
        static std::expected<PseudoConsole, std::error_code> make(short rows, short columns);

#ifdef _WIN32
        void close();
#endif

        void resize(short rows, short columns);
        std::expected<ChildProcess, std::error_code> spawn(Command command);

        Pipe &master();

    private:
        zero::os::process::PseudoConsole mPseudoConsole;
        Pipe mPipe;
    };

    class Command {
    public:
        using Stdio = zero::os::process::Command::Stdio;

        explicit Command(std::filesystem::path path);

    private:
        [[nodiscard]] std::expected<ChildProcess, std::error_code>
        spawn(const std::array<Stdio, 3> &defaultStdio) const;

    public:
        template<zero::meta::Mutable Self>
        Self &&arg(this Self &&self, std::string arg) {
            self.mCommand.arg(std::move(arg));
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&args(this Self &&self, std::vector<std::string> args) {
            self.mCommand.args(std::move(args));
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&currentDirectory(this Self &&self, std::filesystem::path path) {
            self.mCommand.currentDirectory(std::move(path));
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&env(this Self &&self, std::string key, std::string value) {
            self.mCommand.env(std::move(key), std::move(value));
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&envs(this Self &&self, std::map<std::string, std::string> envs) {
            self.mCommand.envs(std::move(envs));
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&clearEnv(this Self &&self) {
            self.mCommand.clearEnv();
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&inheritedResource(this Self &&self, zero::os::Resource resource) {
            self.mCommand.inheritedResource(std::move(resource));
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&inheritedResources(this Self &&self, std::vector<zero::os::Resource> resource) {
            self.mCommand.inheritedResources(std::move(resource));
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&stdInput(this Self &&self, Stdio stdio) {
            self.mCommand.stdInput(std::move(stdio));
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&stdOutput(this Self &&self, Stdio stdio) {
            self.mCommand.stdOutput(std::move(stdio));
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&stdError(this Self &&self, Stdio stdio) {
            self.mCommand.stdError(std::move(stdio));
            return std::forward<Self>(self);
        }

#ifdef _WIN32
        template<zero::meta::Mutable Self>
        Self &&creationFlags(this Self &&self, const DWORD flags) {
            self.mCommand.creationFlags(flags);
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&showWindow(this Self &&self, const WORD show) {
            self.mCommand.showWindow(show);
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&rawAttribute(this Self &&self, const DWORD_PTR attribute, const PVOID value, const SIZE_T size) {
            self.mCommand.rawAttribute(attribute, value, size);
            return std::forward<Self>(self);
        }
#else
        template<zero::meta::Mutable Self>
        Self &&setSID(this Self &&self) {
            self.mCommand.setSID();
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&arg0(this Self &&self, std::string name) {
            self.mCommand.arg0(std::move(name));
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&processGroup(this Self &&self, const pid_t pgid) {
            self.mCommand.processGroup(pgid);
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&uid(this Self &&self, const uid_t uid) {
            self.mCommand.uid(uid);
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&gid(this Self &&self, const gid_t gid) {
            self.mCommand.gid(gid);
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&groups(this Self &&self, std::vector<gid_t> groups) {
            self.mCommand.groups(std::move(groups));
            return std::forward<Self>(self);
        }

        template<zero::meta::Mutable Self>
        Self &&preExec(this Self &&self, std::function<std::expected<void, std::error_code>()> f) {
            self.mCommand.preExec(std::move(f));
            return std::forward<Self>(self);
        }
#endif

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
