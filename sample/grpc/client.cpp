#include <asyncio/error.h>
#include <asyncio/channel.h>
#include <zero/cmdline.h>
#include <grpcpp/grpcpp.h>
#include <sample.grpc.pb.h>

template<typename T>
class Reader final : public grpc::ClientReadReactor<T> {
public:
    explicit Reader(std::shared_ptr<grpc::ClientContext> context) : mContext{std::move(context)} {
    }

    void OnDone(const grpc::Status &status) override {
        if (!status.ok()) {
            mDonePromise.reject(status.error_message());
            return;
        }

        mDonePromise.resolve();
    }

    void OnReadDone(const bool ok) override {
        std::exchange(mReadPromise, std::nullopt)->resolve(ok);
    }

    asyncio::task::Task<std::optional<T>> read() {
        T element;

        asyncio::Promise<bool> promise;
        auto future = promise.getFuture();

        mReadPromise.emplace(std::move(promise));
        grpc::ClientReadReactor<T>::StartRead(&element);

        if (!co_await asyncio::task::Cancellable{
            std::move(future),
            [this]() -> std::expected<void, std::error_code> {
                mContext->TryCancel();
                return {};
            }
        })
            co_return std::nullopt;

        co_return element;
    }

    asyncio::task::Task<void> done() {
        if (const auto result = co_await mDonePromise.getFuture(); !result)
            throw co_await asyncio::error::StacktraceError<std::runtime_error>::make(result.error());
    }

private:
    std::shared_ptr<grpc::ClientContext> mContext;
    asyncio::Promise<void, std::string> mDonePromise;
    std::optional<asyncio::Promise<bool>> mReadPromise;
};

template<typename T>
class Writer final : public grpc::ClientWriteReactor<T> {
public:
    explicit Writer(std::shared_ptr<grpc::ClientContext> context) : mContext{std::move(context)} {
    }

    void OnDone(const grpc::Status &status) override {
        if (!status.ok()) {
            mDonePromise.reject(status.error_message());
            return;
        }

        mDonePromise.resolve();
    }

    void OnWriteDone(const bool ok) override {
        std::exchange(mWritePromise, std::nullopt)->resolve(ok);
    }

    void OnWritesDoneDone(const bool ok) override {
        std::exchange(mWriteDonePromise, std::nullopt)->resolve(ok);
    }

    asyncio::task::Task<bool> write(const T element) {
        asyncio::Promise<bool> promise;
        auto future = promise.getFuture();

        mWritePromise.emplace(std::move(promise));
        grpc::ClientWriteReactor<T>::StartWrite(&element);

        co_return co_await asyncio::task::Cancellable{
            std::move(future),
            [this]() -> std::expected<void, std::error_code> {
                mContext->TryCancel();
                return {};
            }
        };
    }

    asyncio::task::Task<bool> writeDone() {
        asyncio::Promise<bool> promise;
        auto future = promise.getFuture();

        mWriteDonePromise.emplace(std::move(promise));
        grpc::ClientWriteReactor<T>::StartWritesDone();

        co_return co_await asyncio::task::Cancellable{
            std::move(future),
            [this]() -> std::expected<void, std::error_code> {
                mContext->TryCancel();
                return {};
            }
        };
    }

    asyncio::task::Task<void> done() {
        if (const auto result = co_await mDonePromise.getFuture(); !result)
            throw co_await asyncio::error::StacktraceError<std::runtime_error>::make(result.error());
    }

private:
    std::shared_ptr<grpc::ClientContext> mContext;
    asyncio::Promise<void, std::string> mDonePromise;
    std::optional<asyncio::Promise<bool>> mWritePromise;
    std::optional<asyncio::Promise<bool>> mWriteDonePromise;
};

template<typename RequestElement, typename ResponseElement>
class Stream final : public grpc::ClientBidiReactor<RequestElement, ResponseElement> {
public:
    explicit Stream(std::shared_ptr<grpc::ClientContext> context) : mContext{std::move(context)} {
    }

    void OnDone(const grpc::Status &status) override {
        if (!status.ok()) {
            mDonePromise.reject(status.error_message());
            return;
        }

        mDonePromise.resolve();
    }

    void OnReadDone(const bool ok) override {
        std::exchange(mReadPromise, std::nullopt)->resolve(ok);
    }

    void OnWriteDone(const bool ok) override {
        std::exchange(mWritePromise, std::nullopt)->resolve(ok);
    }

    void OnWritesDoneDone(const bool ok) override {
        std::exchange(mWriteDonePromise, std::nullopt)->resolve(ok);
    }

    asyncio::task::Task<std::optional<ResponseElement>> read() {
        ResponseElement element;

        asyncio::Promise<bool> promise;
        auto future = promise.getFuture();

        mReadPromise.emplace(std::move(promise));
        grpc::ClientBidiReactor<RequestElement, ResponseElement>::StartRead(&element);

        if (!co_await asyncio::task::Cancellable{
            std::move(future),
            [this]() -> std::expected<void, std::error_code> {
                mContext->TryCancel();
                return {};
            }
        })
            co_return std::nullopt;

        co_return element;
    }

    asyncio::task::Task<bool> write(const RequestElement element) {
        asyncio::Promise<bool> promise;
        auto future = promise.getFuture();

        mWritePromise.emplace(std::move(promise));
        grpc::ClientBidiReactor<RequestElement, ResponseElement>::StartWrite(&element);

        co_return co_await asyncio::task::Cancellable{
            std::move(future),
            [this]() -> std::expected<void, std::error_code> {
                mContext->TryCancel();
                return {};
            }
        };
    }

    asyncio::task::Task<bool> writeDone() {
        asyncio::Promise<bool> promise;
        auto future = promise.getFuture();

        mWriteDonePromise.emplace(std::move(promise));
        grpc::ClientBidiReactor<RequestElement, ResponseElement>::StartWritesDone();

        co_return co_await asyncio::task::Cancellable{
            std::move(future),
            [this]() -> std::expected<void, std::error_code> {
                mContext->TryCancel();
                return {};
            }
        };
    }

    asyncio::task::Task<void> done() {
        if (const auto result = co_await mDonePromise.getFuture(); !result)
            throw co_await asyncio::error::StacktraceError<std::runtime_error>::make(result.error());
    }

private:
    std::shared_ptr<grpc::ClientContext> mContext;
    asyncio::Promise<void, std::string> mDonePromise;
    std::optional<asyncio::Promise<bool>> mReadPromise;
    std::optional<asyncio::Promise<bool>> mWritePromise;
    std::optional<asyncio::Promise<bool>> mWriteDonePromise;
};

template<typename T>
class GenericClient {
    using Stub = T::Stub;
    using AsyncStub = class Stub::async;

public:
    explicit GenericClient(std::unique_ptr<Stub> stub) : mStub{std::move(stub)} {
    }

protected:
    template<typename Request, typename Response>
    asyncio::task::Task<Response>
    call(
        void (AsyncStub::*method)(grpc::ClientContext *,
                                  const Request *,
                                  Response *,
                                  std::function<void(grpc::Status)>),
        const std::shared_ptr<grpc::ClientContext> context,
        const Request request
    ) {
        Response response;
        asyncio::Promise<void, std::string> promise;

        std::invoke(
            method,
            mStub->async(),
            context.get(),
            &request,
            &response,
            [&](const grpc::Status &status) {
                if (!status.ok()) {
                    promise.reject(status.error_message());
                    return;
                }

                promise.resolve();
            }
        );

        if (const auto result = co_await asyncio::task::Cancellable{
            promise.getFuture(),
            [&]() -> std::expected<void, std::error_code> {
                context->TryCancel();
                return {};
            }
        }; !result)
            throw co_await asyncio::error::StacktraceError<std::runtime_error>::make(result.error());

        co_return response;
    }

    template<typename Request, typename Element>
    asyncio::task::Task<void>
    call(
        void (AsyncStub::*method)(grpc::ClientContext *, const Request *, grpc::ClientReadReactor<Element> *),
        const std::shared_ptr<grpc::ClientContext> context,
        const Request request,
        asyncio::Sender<Element> sender
    ) {
        Reader<Element> reader{context};
        std::invoke(method, mStub->async(), context.get(), &request, &reader);

        reader.AddHold();
        reader.StartCall();

        const auto result = co_await asyncio::error::capture(
            asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
                while (true) {
                    auto element = co_await reader.read();

                    if (!element)
                        break;

                    co_await asyncio::error::guard(sender.send(*std::move(element)));
                }
            })
        );

        reader.RemoveHold();
        co_await reader.done();

        if (!result)
            std::rethrow_exception(result.error());
    }

    template<typename Response, typename Element>
    asyncio::task::Task<Response>
    call(
        void (AsyncStub::*method)(grpc::ClientContext *, Response *, grpc::ClientWriteReactor<Element> *),
        const std::shared_ptr<grpc::ClientContext> context,
        asyncio::Receiver<Element> receiver
    ) {
        Response response;
        Writer<Element> writer{context};

        std::invoke(method, mStub->async(), context.get(), &response, &writer);

        writer.AddHold();
        writer.StartCall();

        const auto result = co_await asyncio::error::capture(
            asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
                while (true) {
                    auto element = co_await receiver.receive();

                    if (!element) {
                        if (!co_await writer.writeDone())
                            fmt::print(stderr, "Write done failed\n");

                        if (const auto &error = element.error(); error != asyncio::ReceiveError::Disconnected)
                            throw co_await asyncio::error::StacktraceError<std::system_error>::make(error);

                        break;
                    }

                    co_await writer.write(*std::move(element));
                }
            })
        );

        writer.RemoveHold();
        co_await writer.done();

        if (!result)
            std::rethrow_exception(result.error());

        co_return response;
    }

    template<typename RequestElement, typename ResponseElement>
    asyncio::task::Task<void>
    call(
        void (AsyncStub::*method)(grpc::ClientContext *, grpc::ClientBidiReactor<RequestElement, ResponseElement> *),
        const std::shared_ptr<grpc::ClientContext> context,
        asyncio::Receiver<RequestElement> receiver,
        asyncio::Sender<ResponseElement> sender
    ) {
        Stream<RequestElement, ResponseElement> stream{context};
        std::invoke(method, mStub->async(), context.get(), &stream);

        stream.AddHold();
        stream.StartCall();

        const auto result = co_await asyncio::error::capture(
            all(
                asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
                    while (true) {
                        auto element = co_await stream.read();

                        if (!element)
                            break;

                        if (const auto res = co_await sender.send(*std::move(element)); !res) {
                            context->TryCancel();
                            throw co_await asyncio::error::StacktraceError<std::system_error>::make(res.error());
                        }
                    }
                }),
                asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
                    while (true) {
                        auto element = co_await receiver.receive();

                        if (!element) {
                            if (!co_await stream.writeDone())
                                fmt::print(stderr, "Write done failed\n");

                            if (const auto &error = element.error(); error != asyncio::ReceiveError::Disconnected)
                                throw co_await asyncio::error::StacktraceError<std::system_error>::make(error);

                            break;
                        }

                        co_await stream.write(*std::move(element));
                    }
                })
            )
        );

        stream.RemoveHold();
        co_await stream.done();

        if (!result)
            std::rethrow_exception(result.error());
    }

private:
    std::unique_ptr<Stub> mStub;
};

class Client final : public GenericClient<sample::SampleService> {
public:
    using GenericClient::GenericClient;

    static Client make(const std::string &address) {
        return Client{sample::SampleService::NewStub(grpc::CreateChannel(address, grpc::InsecureChannelCredentials()))};
    }

    asyncio::task::Task<sample::EchoResponse>
    echo(
        sample::EchoRequest request,
        std::unique_ptr<grpc::ClientContext> context = std::make_unique<grpc::ClientContext>()
    ) {
        co_return co_await call(&sample::SampleService::Stub::async::Echo, std::move(context), std::move(request));
    }

    asyncio::task::Task<void>
    getNumbers(
        sample::GetNumbersRequest request,
        asyncio::Sender<sample::Number> sender,
        std::unique_ptr<grpc::ClientContext> context = std::make_unique<grpc::ClientContext>()
    ) {
        co_await call(
            &sample::SampleService::Stub::async::GetNumbers,
            std::move(context),
            std::move(request),
            std::move(sender)
        );
    }

    asyncio::task::Task<sample::SumResponse> sum(
        asyncio::Receiver<sample::Number> receiver,
        std::unique_ptr<grpc::ClientContext> context = std::make_unique<grpc::ClientContext>()
    ) {
        co_return co_await call(&sample::SampleService::Stub::async::Sum, std::move(context), std::move(receiver));
    }

    asyncio::task::Task<void>
    chat(
        asyncio::Receiver<sample::ChatMessage> receiver,
        asyncio::Sender<sample::ChatMessage> sender,
        std::unique_ptr<grpc::ClientContext> context = std::make_unique<grpc::ClientContext>()
    ) {
        co_return co_await call(
            &sample::SampleService::Stub::async::Chat,
            std::move(context),
            std::move(receiver),
            std::move(sender)
        );
    }
};

asyncio::task::Task<void> asyncMain(const int argc, char *argv[]) {
    zero::Cmdline cmdline;

    cmdline.add<std::string>("address", "gRPC server address");
    cmdline.parse(argc, argv);

    const auto address = cmdline.get<std::string>("address");

    auto client = Client::make(address);

    co_await all(
        asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
            sample::EchoRequest request;
            request.set_message("Hello gRPC!");

            const auto response = co_await client.echo(request);
            fmt::print("Echo response: {}, timestamp: {}\n", response.message(), response.timestamp());
        }),
        asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
            sample::GetNumbersRequest request;

            request.set_value(1);
            request.set_count(5);

            auto [sender, receiver] = asyncio::channel<sample::Number>();

            const auto result = co_await all(
                client.getNumbers(request, std::move(sender)),
                client.sum(std::move(receiver))
            );

            const auto &response = std::get<sample::SumResponse>(result);
            fmt::print("Sum result: {}, count: {}\n", response.total(), response.count());
        }),
        asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
            auto [inboundSender, inboundReceiver] = asyncio::channel<sample::ChatMessage>();
            auto [outboundSender, outboundReceiver] = asyncio::channel<sample::ChatMessage>();

            co_await all(
                client.chat(std::move(outboundReceiver), std::move(inboundSender)),
                asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
                    sample::ChatMessage message;

                    message.set_user("Client");
                    message.set_timestamp(std::time(nullptr));
                    message.set_content("Hello gRPC server!");

                    co_await asyncio::error::guard(outboundSender.send(std::move(message)));
                    outboundSender.close();
                }),
                asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
                    const auto message = co_await asyncio::error::guard(inboundReceiver.receive());
                    fmt::print(
                        "Received chat message: {}, user: {}, timestamp: {}\n",
                        message.content(),
                        message.user(),
                        message.timestamp()
                    );
                })
            );
        })
    );
}
