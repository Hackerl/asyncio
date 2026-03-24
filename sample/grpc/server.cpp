#include <asyncio/error.h>
#include <asyncio/thread.h>
#include <asyncio/signal.h>
#include <asyncio/sync/event.h>
#include <zero/cmdline.h>
#include <zero/interface.h>
#include <zero/formatter.h>
#include <grpcpp/grpcpp.h>
#include <sample.grpc.pb.h>

template<typename Element>
class Reader {
public:
    class IReader : public zero::Interface {
    public:
        virtual asyncio::task::Task<std::optional<Element>> read() = 0;
    };

    template<typename Response>
    class Impl : public IReader {
    public:
        Impl(
            std::shared_ptr<grpc::ServerContext> context,
            std::shared_ptr<grpc::ServerAsyncReader<Response, Element>> reader
        ) : mContext{std::move(context)}, mReader{std::move(reader)} {
        }

        asyncio::task::Task<std::optional<Element>> read() override {
            Element element;
            asyncio::Promise<bool> promise;

            mReader->Read(&element, &promise);

            if (!*co_await asyncio::task::CancellableFuture{
                promise.getFuture(),
                [this]() -> std::expected<void, std::error_code> {
                    mContext->TryCancel();
                    return {};
                }
            }) {
                if (co_await asyncio::task::cancelled)
                    throw co_await asyncio::error::StacktraceError<std::system_error>::make(
                        asyncio::task::Error::Cancelled
                    );

                co_return std::nullopt;
            }

            co_return element;
        }

    private:
        std::shared_ptr<grpc::ServerContext> mContext;
        std::shared_ptr<grpc::ServerAsyncReader<Response, Element>> mReader;
    };

    explicit Reader(std::unique_ptr<IReader> reader) : mReader{std::move(reader)} {
    }

    asyncio::task::Task<std::optional<Element>> read() {
        co_return co_await mReader->read();
    }

private:
    std::unique_ptr<IReader> mReader;
};

template<typename T>
class Writer {
public:
    Writer(std::shared_ptr<grpc::ServerContext> context, std::shared_ptr<grpc::ServerAsyncWriter<T>> writer)
        : mContext{std::move(context)}, mWriter{std::move(writer)} {
    }

    asyncio::task::Task<void> write(const T element) {
        asyncio::Promise<bool> promise;
        mWriter->Write(element, &promise);

        if (!*co_await asyncio::task::CancellableFuture{
            promise.getFuture(),
            [this]() -> std::expected<void, std::error_code> {
                mContext->TryCancel();
                return {};
            }
        })
            throw co_await asyncio::error::StacktraceError<std::system_error>::make(asyncio::task::Error::Cancelled);
    }

private:
    std::shared_ptr<grpc::ServerContext> mContext;
    std::shared_ptr<grpc::ServerAsyncWriter<T>> mWriter;
};

template<typename RequestElement, typename ResponseElement>
class Stream {
public:
    Stream(
        std::shared_ptr<grpc::ServerContext> context,
        std::shared_ptr<grpc::ServerAsyncReaderWriter<ResponseElement, RequestElement>> stream
    ) : mContext{std::move(context)}, mStream{std::move(stream)} {
    }

    asyncio::task::Task<std::optional<RequestElement>> read() {
        RequestElement element;
        asyncio::Promise<bool> promise;

        mStream->Read(&element, &promise);

        if (!*co_await asyncio::task::CancellableFuture{
            promise.getFuture(),
            [this]() -> std::expected<void, std::error_code> {
                mContext->TryCancel();
                return {};
            }
        }) {
            if (co_await asyncio::task::cancelled)
                throw co_await asyncio::error::StacktraceError<std::system_error>::make(
                    asyncio::task::Error::Cancelled
                );

            co_return std::nullopt;
        }

        co_return element;
    }

    asyncio::task::Task<void> write(const ResponseElement element) {
        asyncio::Promise<bool> promise;
        mStream->Write(element, &promise);

        if (!*co_await asyncio::task::CancellableFuture{
            promise.getFuture(),
            [this]() -> std::expected<void, std::error_code> {
                mContext->TryCancel();
                return {};
            }
        })
            throw co_await asyncio::error::StacktraceError<std::system_error>::make(asyncio::task::Error::Cancelled);
    }

private:
    std::shared_ptr<grpc::ServerContext> mContext;
    std::shared_ptr<grpc::ServerAsyncReaderWriter<ResponseElement, RequestElement>> mStream;
};

template<typename T>
class GenericServer {
    using AsyncService = T::AsyncService;

public:
    GenericServer(
        std::unique_ptr<grpc::Server> server,
        std::unique_ptr<AsyncService> service,
        std::unique_ptr<grpc::ServerCompletionQueue> completionQueue
    ) : mServer{std::move(server)}, mService{std::move(service)}, mCompletionQueue{std::move(completionQueue)} {
    }

    virtual ~GenericServer() = default;

protected:
    template<typename Service, typename Request, typename Response>
        requires std::derived_from<AsyncService, Service>
    asyncio::task::Task<void>
    handle(
        void (Service::*method)(grpc::ServerContext *,
                                Request *,
                                grpc::ServerAsyncResponseWriter<Response> *,
                                grpc::CompletionQueue *,
                                grpc::ServerCompletionQueue *,
                                void *),
        std::function<asyncio::task::Task<Response>(Request)> handler
    ) {
        asyncio::task::TaskGroup group;

        while (true) {
            auto context = std::make_unique<grpc::ServerContext>();

            Request request;
            grpc::ServerAsyncResponseWriter<Response> writer{context.get()};

            {
                asyncio::Promise<bool> promise;

                std::invoke(
                    method,
                    mService,
                    context.get(),
                    &request,
                    &writer,
                    mCompletionQueue.get(),
                    mCompletionQueue.get(),
                    &promise
                );

                if (!*co_await promise.getFuture())
                    break;
            }

            auto task = asyncio::task::spawn(
                [
                    &handler, context = std::move(context), request = std::move(request), writer = std::move(writer)
                ]() mutable -> asyncio::task::Task<void> {
                    const auto response = co_await asyncio::error::capture(handler(std::move(request)));

                    asyncio::Promise<bool> promise;

                    if (response) {
                        writer.Finish(*response, grpc::Status::OK, &promise);
                    }
                    else {
                        writer.FinishWithError(
                            grpc::Status{grpc::StatusCode::INTERNAL, fmt::to_string(response.error())},
                            &promise
                        );
                    }

                    if (!*co_await asyncio::task::CancellableFuture{
                        promise.getFuture(),
                        [&]() -> std::expected<void, std::error_code> {
                            context->TryCancel();
                            return {};
                        }
                    })
                        throw co_await asyncio::error::StacktraceError<std::system_error>::make(
                            asyncio::task::Error::Cancelled
                        );
                }
            );

            group.add(task);

            task.future().fail([](const auto &e) {
                fmt::print(stderr, "Unhandled exception: {}", e);
            });
        }

        std::ignore = group.cancel();
        co_await group;
    }

    template<typename Service, typename Request, typename Element>
        requires std::derived_from<AsyncService, Service>
    asyncio::task::Task<void>
    handle(
        void (Service::*method)(grpc::ServerContext *,
                                Request *,
                                grpc::ServerAsyncWriter<Element> *,
                                grpc::CompletionQueue *,
                                grpc::ServerCompletionQueue *,
                                void *),
        std::function<asyncio::task::Task<void>(Request, Writer<Element>)> handler
    ) {
        asyncio::task::TaskGroup group;

        while (true) {
            auto context = std::make_shared<grpc::ServerContext>();

            Request request;
            auto writer = std::make_shared<grpc::ServerAsyncWriter<Element>>(context.get());

            {
                asyncio::Promise<bool> promise;

                std::invoke(
                    method,
                    mService,
                    context.get(),
                    &request,
                    writer.get(),
                    mCompletionQueue.get(),
                    mCompletionQueue.get(),
                    &promise
                );

                if (!*co_await promise.getFuture())
                    break;
            }

            auto task = asyncio::task::spawn(
                [
                    &handler, context = std::move(context), request = std::move(request), writer = std::move(writer)
                ]() mutable -> asyncio::task::Task<void> {
                    const auto result = co_await asyncio::error::capture(
                        handler(std::move(request), Writer{context, writer})
                    );

                    asyncio::Promise<bool> promise;

                    if (result) {
                        writer->Finish(grpc::Status::OK, &promise);
                    }
                    else {
                        writer->Finish(
                            grpc::Status{grpc::StatusCode::INTERNAL, fmt::to_string(result.error())},
                            &promise
                        );
                    }

                    if (!*co_await asyncio::task::CancellableFuture{
                        promise.getFuture(),
                        [&]() -> std::expected<void, std::error_code> {
                            context->TryCancel();
                            return {};
                        }
                    })
                        throw co_await asyncio::error::StacktraceError<std::system_error>::make(
                            asyncio::task::Error::Cancelled
                        );
                }
            );

            group.add(task);

            task.future().fail([](const auto &e) {
                fmt::print(stderr, "Unhandled exception: {}", e);
            });
        }

        std::ignore = group.cancel();
        co_await group;
    }

    template<typename Service, typename Element, typename Response>
        requires std::derived_from<AsyncService, Service>
    asyncio::task::Task<void>
    handle(
        void (Service::*method)(grpc::ServerContext *,
                                grpc::ServerAsyncReader<Response, Element> *,
                                grpc::CompletionQueue *,
                                grpc::ServerCompletionQueue *,
                                void *),
        std::function<asyncio::task::Task<Response>(Reader<Element>)> handler
    ) {
        asyncio::task::TaskGroup group;

        while (true) {
            auto context = std::make_shared<grpc::ServerContext>();
            auto reader = std::make_shared<grpc::ServerAsyncReader<Response, Element>>(context.get());

            {
                asyncio::Promise<bool> promise;

                std::invoke(
                    method,
                    mService,
                    context.get(),
                    reader.get(),
                    mCompletionQueue.get(),
                    mCompletionQueue.get(),
                    &promise
                );

                if (!*co_await promise.getFuture())
                    break;
            }

            auto task = asyncio::task::spawn(
                [&handler, context = std::move(context), reader = std::move(reader)]() -> asyncio::task::Task<void> {
                    using Impl = Reader<Element>::template Impl<Response>;

                    const auto response = co_await asyncio::error::capture(
                        handler(Reader<Element>{std::make_unique<Impl>(context, reader)})
                    );

                    asyncio::Promise<bool> promise;

                    if (response) {
                        reader->Finish(*response, grpc::Status::OK, &promise);
                    }
                    else {
                        reader->FinishWithError(
                            grpc::Status{grpc::StatusCode::INTERNAL, fmt::to_string(response.error())},
                            &promise
                        );
                    }

                    if (!*co_await asyncio::task::CancellableFuture{
                        promise.getFuture(),
                        [&]() -> std::expected<void, std::error_code> {
                            context->TryCancel();
                            return {};
                        }
                    })
                        throw co_await asyncio::error::StacktraceError<std::system_error>::make(
                            asyncio::task::Error::Cancelled
                        );
                }
            );

            group.add(task);

            task.future().fail([](const auto &e) {
                fmt::print(stderr, "Unhandled exception: {}", e);
            });
        }

        std::ignore = group.cancel();
        co_await group;
    }

    template<typename Service, typename RequestElement, typename ResponseElement>
        requires std::derived_from<AsyncService, Service>
    asyncio::task::Task<void>
    handle(
        void (Service::*method)(grpc::ServerContext *,
                                grpc::ServerAsyncReaderWriter<ResponseElement, RequestElement> *,
                                grpc::CompletionQueue *,
                                grpc::ServerCompletionQueue *,
                                void *),
        std::function<asyncio::task::Task<void>(Stream<RequestElement, ResponseElement>)> handler
    ) {
        asyncio::task::TaskGroup group;

        while (true) {
            auto context = std::make_shared<grpc::ServerContext>();
            auto stream = std::make_shared<grpc::ServerAsyncReaderWriter<ResponseElement, RequestElement>>(
                context.get()
            );

            {
                asyncio::Promise<bool> promise;

                std::invoke(
                    method,
                    mService,
                    context.get(),
                    stream.get(),
                    mCompletionQueue.get(),
                    mCompletionQueue.get(),
                    &promise
                );

                if (!*co_await promise.getFuture())
                    break;
            }

            auto task = asyncio::task::spawn(
                [&handler, context = std::move(context), stream = std::move(stream)]() -> asyncio::task::Task<void> {
                    const auto result = co_await asyncio::error::capture(handler(Stream{context, stream}));

                    asyncio::Promise<bool> promise;

                    if (result) {
                        stream->Finish(grpc::Status::OK, &promise);
                    }
                    else {
                        stream->Finish(
                            grpc::Status{grpc::StatusCode::INTERNAL, fmt::to_string(result.error())},
                            &promise
                        );
                    }

                    if (!*co_await asyncio::task::CancellableFuture{
                        promise.getFuture(),
                        [&]() -> std::expected<void, std::error_code> {
                            context->TryCancel();
                            return {};
                        }
                    })
                        throw co_await asyncio::error::StacktraceError<std::system_error>::make(
                            asyncio::task::Error::Cancelled
                        );
                }
            );

            group.add(task);

            task.future().fail([](const auto &e) {
                fmt::print(stderr, "Unhandled exception: {}", e);
            });
        }

        std::ignore = group.cancel();
        co_await group;
    }

    virtual asyncio::task::Task<void> dispatch() = 0;

public:
    asyncio::task::Task<void> shutdown() {
        co_await asyncio::toThread([=, this] {
            mServer->Shutdown();
            mCompletionQueue->Shutdown();
        });
    }

    virtual asyncio::task::Task<void> run() {
        co_await all(
            dispatch(),
            asyncio::toThread(
                [this] {
                    void *tag{};
                    bool ok{};

                    while (mCompletionQueue->Next(&tag, &ok)) {
                        static_cast<asyncio::Promise<bool> *>(tag)->resolve(ok);
                    }
                }
            )
        );
    }

private:
    std::unique_ptr<grpc::Server> mServer;
    std::unique_ptr<AsyncService> mService;
    std::unique_ptr<grpc::ServerCompletionQueue> mCompletionQueue;
};

class Server final : public GenericServer<sample::SampleService> {
public:
    using GenericServer::GenericServer;

    static Server make(const std::string &address) {
        auto service = std::make_unique<sample::SampleService::AsyncService>();

        grpc::ServerBuilder builder;

        builder.AddListeningPort(address, grpc::InsecureServerCredentials());
        builder.RegisterService(service.get());

        auto completionQueue = builder.AddCompletionQueue();
        auto server = builder.BuildAndStart();

        return {std::move(server), std::move(service), std::move(completionQueue)};
    }

    static asyncio::task::Task<sample::EchoResponse> echo(const sample::EchoRequest request) {
        sample::EchoResponse response;
        response.set_message(request.message());
        co_return response;
    }

    static asyncio::task::Task<void>
    getNumbers(const sample::GetNumbersRequest request, Writer<sample::Number> writer) {
        const auto value = request.value();
        const auto count = request.count();

        for (int i{0}; i < count; ++i) {
            sample::Number number;
            number.set_value(value + i);

            co_await writer.write(number);
        }
    }

    static asyncio::task::Task<sample::SumResponse> sum(Reader<sample::Number> reader) {
        int sum{0};
        int count{0};

        while (const auto number = co_await reader.read()) {
            sum += number->value();
            ++count;
        }

        sample::SumResponse response;

        response.set_total(sum);
        response.set_count(count);

        co_return response;
    }

    static asyncio::task::Task<void> chat(Stream<sample::ChatMessage, sample::ChatMessage> stream) {
        while (const auto message = co_await stream.read()) {
            sample::ChatMessage response;

            response.set_user("Server");
            response.set_timestamp(std::time(nullptr));
            response.set_content(fmt::format("Echo: {}", message->content()));

            co_await stream.write(response);
        }
    }

    asyncio::task::Task<void> dispatch() override {
        co_await all(
            handle(&sample::SampleService::AsyncService::RequestEcho, std::function{echo}),
            handle(&sample::SampleService::AsyncService::RequestGetNumbers, std::function{getNumbers}),
            handle(&sample::SampleService::AsyncService::RequestSum, std::function{sum}),
            handle(&sample::SampleService::AsyncService::RequestChat, std::function{chat})
        );
    }
};

asyncio::task::Task<void> asyncMain(const int argc, char *argv[]) {
    zero::Cmdline cmdline;

    cmdline.add<std::string>("address", "Address to bind");
    cmdline.parse(argc, argv);

    const auto address = cmdline.get<std::string>("address");

    auto server = Server::make(address);
    auto signal = asyncio::Signal::make();

    co_await race(
        asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
            asyncio::sync::Event event;

            co_await asyncio::task::CancellableTask{
                all(
                    server.run(),
                    asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
                        co_await asyncio::error::guard(event.wait());
                        co_await server.shutdown();
                    })
                ),
                [&]() -> std::expected<void, std::error_code> {
                    event.set();
                    return {};
                }
            };
        }),
        asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
            co_await asyncio::error::guard(signal.on(SIGINT));
        })
    );
}
