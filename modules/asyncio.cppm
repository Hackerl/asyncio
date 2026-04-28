module;

#include <asyncio/binary.h>
#include <asyncio/buffer.h>
#include <asyncio/channel.h>
#include <asyncio/event_loop.h>
#include <asyncio/fs.h>
#include <asyncio/io.h>
#include <asyncio/pipe.h>
#include <asyncio/poll.h>
#include <asyncio/process.h>
#include <asyncio/promise.h>
#include <asyncio/signal.h>
#include <asyncio/stream.h>
#include <asyncio/task.h>
#include <asyncio/thread.h>
#include <asyncio/time.h>
#include <asyncio/uv.h>

export module asyncio;

export import :http;
export import :net;
export import :sync;

export namespace asyncio {
    namespace binary {
        using asyncio::binary::readLE;
        using asyncio::binary::readBE;
        using asyncio::binary::writeLE;
        using asyncio::binary::writeBE;
    }

    using asyncio::BufReader;
    using asyncio::BufWriter;
    using asyncio::BufReaderError;

    using asyncio::ChannelCore;
    using asyncio::Sender;
    using asyncio::Receiver;
    using asyncio::Channel;
    using asyncio::channel;
    using asyncio::TrySendError;
    using asyncio::SendSyncError;
    using asyncio::SendError;
    using asyncio::TryReceiveError;
    using asyncio::ReceiveSyncError;
    using asyncio::ReceiveError;
    using asyncio::ChannelError;

    using asyncio::EventLoop;
    using asyncio::run;
    using asyncio::reschedule;

    using asyncio::File;
    using asyncio::open;
    using asyncio::read;
    using asyncio::readString;
    using asyncio::write;
    using asyncio::absolute;
    using asyncio::canonical;
    using asyncio::weaklyCanonical;
    using asyncio::relative;
    using asyncio::proximate;
    using asyncio::copy;
    using asyncio::copyFile;
    using asyncio::copySymlink;
    using asyncio::createDirectory;
    using asyncio::createDirectories;
    using asyncio::createHardLink;
    using asyncio::createSymlink;
    using asyncio::createDirectorySymlink;
    using asyncio::currentPath;
    using asyncio::exists;
    using asyncio::equivalent;
    using asyncio::fileSize;
    using asyncio::hardLinkCount;
    using asyncio::lastWriteTime;
    using asyncio::permissions;
    using asyncio::readSymlink;
    using asyncio::remove;
    using asyncio::removeAll;
    using asyncio::rename;
    using asyncio::resizeFile;
    using asyncio::space;
    using asyncio::status;
    using asyncio::symlinkStatus;
    using asyncio::temporaryDirectory;
    using asyncio::isBlockFile;
    using asyncio::isCharacterFile;
    using asyncio::isDirectory;
    using asyncio::isEmpty;
    using asyncio::isFIFO;
    using asyncio::isOther;
    using asyncio::isRegularFile;
    using asyncio::isSocket;
    using asyncio::isSymlink;
    using asyncio::DirectoryEntry;
    using asyncio::Asynchronous;
    using asyncio::readDirectory;
    using asyncio::walkDirectory;

    using asyncio::FileDescriptor;
    using asyncio::IFileDescriptor;
    using asyncio::ICloseable;
    using asyncio::IHalfCloseable;
    using asyncio::IReader;
    using asyncio::IWriter;
    using asyncio::ISeekable;
    using asyncio::IBufReader;
    using asyncio::IBufWriter;
    using asyncio::copy;
    using asyncio::StringReader;
    using asyncio::StringWriter;
    using asyncio::BytesReader;
    using asyncio::BytesWriter;
    using asyncio::IOError;

    using asyncio::Pipe;
    using asyncio::PipeListener;

    using asyncio::Poll;

    using asyncio::Process;
    using asyncio::ExitStatus;
    using asyncio::Output;
    using asyncio::ChildProcess;
    using asyncio::Command;
    using asyncio::PseudoConsole;

    using asyncio::Promise;

    using asyncio::Signal;

    using asyncio::Stream;
    using asyncio::Listener;

    namespace task {
        using asyncio::task::Awaitable;
        using asyncio::task::NoExceptAwaitable;
        using asyncio::task::TaskGroup;
        using asyncio::task::Frame;
        using asyncio::task::CancellableFuture;
        using asyncio::task::CancellableTask;
        using asyncio::task::Cancelled;
        using asyncio::task::Lock;
        using asyncio::task::Unlock;
        using asyncio::task::cancelled;
        using asyncio::task::lock;
        using asyncio::task::unlock;
        using asyncio::task::callback_result_t;
        using asyncio::task::Task;
        using asyncio::task::Promise;
        using asyncio::task::all_ranges_future_t;
        using asyncio::task::all_ranges_value_t;
        using asyncio::task::all_ranges_error_t;
        using asyncio::task::all;
        using asyncio::task::all_varaidic_future_t;
        using asyncio::task::all_variadic_value_t;
        using asyncio::task::all_variadic_error_t;
        using asyncio::task::all_settled_ranges_future_t;
        using asyncio::task::all_settled_ranges_value_t;
        using asyncio::task::allSettled;
        using asyncio::task::all_settled_variadic_future_t;
        using asyncio::task::all_settled_variadic_value_t;
        using asyncio::task::any_ranges_future_t;
        using asyncio::task::any_ranges_value_t;
        using asyncio::task::any_ranges_error_t;
        using asyncio::task::any;
        using asyncio::task::any_variadic_future_t;
        using asyncio::task::any_variadic_value_t;
        using asyncio::task::any_variadic_error_t;
        using asyncio::task::race_ranges_future_t;
        using asyncio::task::race_ranges_value_t;
        using asyncio::task::race_ranges_error_t;
        using asyncio::task::race;
        using asyncio::task::race_variadic_future_t;
        using asyncio::task::race_variadic_value_t;
        using asyncio::task::race_variadic_error_t;
        using asyncio::task::from;
        using asyncio::task::spawn;
        using asyncio::task::Error;
    }

    using asyncio::toThread;
    using asyncio::toThreadPool;
    using asyncio::ToThreadPoolError;

    using asyncio::timeout;
    using asyncio::TimeoutError;

    namespace uv {
        using asyncio::uv::expected;
        using asyncio::uv::Handle;
        using asyncio::uv::Error;
    }
}
