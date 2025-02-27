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
        using Resource = zero::os::process::Command::Resource;
        using StdioType = zero::os::process::Command::StdioType;

        explicit Command(std::filesystem::path path);

    private:
        [[nodiscard]] std::expected<ChildProcess, std::error_code>
        spawn(const std::array<StdioType, 3> &defaultTypes) const;

    public:
        Command &arg(std::string arg);
        Command &args(std::vector<std::string> args);
        Command &currentDirectory(std::filesystem::path path);
        Command &env(std::string key, std::string value);
        Command &envs(std::map<std::string, std::string> envs);
        Command &inheritedResource(Resource resource);
        Command &inheritedResources(std::vector<Resource> resource);
        Command &clearEnv();
        Command &removeEnv(const std::string &key);
        Command &stdInput(StdioType type);
        Command &stdOutput(StdioType type);
        Command &stdError(StdioType type);

        [[nodiscard]] const std::filesystem::path &program() const;
        [[nodiscard]] const std::vector<std::string> &args() const;
        [[nodiscard]] const std::optional<std::filesystem::path> &currentDirectory() const;
        [[nodiscard]] const std::map<std::string, std::optional<std::string>> &envs() const;
        [[nodiscard]] const std::vector<Resource> &inheritedResources() const;

        [[nodiscard]] std::expected<ChildProcess, std::error_code> spawn() const;
        [[nodiscard]] task::Task<ExitStatus, std::error_code> status() const;
        [[nodiscard]] task::Task<Output, std::error_code> output() const;

    private:
        zero::os::process::Command mCommand;

        friend class PseudoConsole;
    };
}

#endif //ASYNCIO_PROCESS_H
