#include <asyncio/channel.h>
#include <zero/singleton.h>

const char *asyncio::ChannelErrorCategory::name() const noexcept {
    return "asyncio::channel";
}

std::string asyncio::ChannelErrorCategory::message(const int value) const {
    if (static_cast<ChannelError>(value) == ChannelError::DISCONNECTED)
        return "channel disconnected";

    return "unknown";
}

bool asyncio::ChannelErrorCategory::equivalent(const std::error_code &code, const int value) const noexcept {
    if (static_cast<ChannelError>(value) == ChannelError::DISCONNECTED)
        return code == TrySendError::DISCONNECTED ||
            code == SendSyncError::DISCONNECTED ||
            code == SendError::DISCONNECTED ||
            code == TryReceiveError::DISCONNECTED ||
            code == ReceiveSyncError::DISCONNECTED ||
            code == ReceiveError::DISCONNECTED;

    return error_category::equivalent(code, value);
}

std::error_condition asyncio::make_error_condition(const ChannelError e) {
    return {static_cast<int>(e), zero::Singleton<ChannelErrorCategory>::getInstance()};
}

const char *asyncio::TrySendErrorCategory::name() const noexcept {
    return "asyncio::Sender::trySend";
}

std::string asyncio::TrySendErrorCategory::message(const int value) const {
    std::string msg;

    switch (static_cast<TrySendError>(value)) {
    case TrySendError::DISCONNECTED:
        msg = "sending on a disconnected channel";
        break;

    case TrySendError::FULL:
        msg = "sending on a full channel";
        break;

    default:
        msg = "unknown";
        break;
    }

    return msg;
}

std::error_condition asyncio::TrySendErrorCategory::default_error_condition(const int value) const noexcept {
    if (static_cast<TrySendError>(value) == TrySendError::FULL)
        return std::errc::operation_would_block;

    return error_category::default_error_condition(value);
}

std::error_code asyncio::make_error_code(const TrySendError e) {
    return {static_cast<int>(e), zero::Singleton<TrySendErrorCategory>::getInstance()};
}

const char *asyncio::SendSyncErrorCategory::name() const noexcept {
    return "asyncio::Sender::sendSync";
}

std::string asyncio::SendSyncErrorCategory::message(const int value) const {
    std::string msg;

    switch (static_cast<SendSyncError>(value)) {
    case SendSyncError::DISCONNECTED:
        msg = "sending on a disconnected channel";
        break;

    case SendSyncError::TIMEOUT:
        msg = "timed out waiting on send operation";
        break;

    default:
        msg = "unknown";
        break;
    }

    return msg;
}

std::error_condition asyncio::SendSyncErrorCategory::default_error_condition(const int value) const noexcept {
    if (static_cast<SendSyncError>(value) == SendSyncError::TIMEOUT)
        return std::errc::timed_out;

    return error_category::default_error_condition(value);
}

std::error_code asyncio::make_error_code(const SendSyncError e) {
    return {static_cast<int>(e), zero::Singleton<SendSyncErrorCategory>::getInstance()};
}

const char *asyncio::SendErrorCategory::name() const noexcept {
    return "asyncio::Sender::send";
}

std::string asyncio::SendErrorCategory::message(const int value) const {
    std::string msg;

    switch (static_cast<SendError>(value)) {
    case SendError::DISCONNECTED:
        msg = "sending on a disconnected channel";
        break;

    case SendError::CANCELLED:
        msg = "send operation has been cancelled";
        break;

    default:
        msg = "unknown";
        break;
    }

    return msg;
}

std::error_condition asyncio::SendErrorCategory::default_error_condition(const int value) const noexcept {
    if (static_cast<SendError>(value) == SendError::CANCELLED)
        return std::errc::operation_canceled;

    return error_category::default_error_condition(value);
}

std::error_code asyncio::make_error_code(const SendError e) {
    return {static_cast<int>(e), zero::Singleton<SendErrorCategory>::getInstance()};
}

const char *asyncio::TryReceiveErrorCategory::name() const noexcept {
    return "asyncio::Receiver::tryReceive";
}

std::string asyncio::TryReceiveErrorCategory::message(const int value) const {
    std::string msg;

    switch (static_cast<TryReceiveError>(value)) {
    case TryReceiveError::DISCONNECTED:
        msg = "receiving on an empty and disconnected channel";
        break;

    case TryReceiveError::EMPTY:
        msg = "receiving on an empty channel";
        break;

    default:
        msg = "unknown";
        break;
    }

    return msg;
}

std::error_condition asyncio::TryReceiveErrorCategory::default_error_condition(const int value) const noexcept {
    if (static_cast<TryReceiveError>(value) == TryReceiveError::EMPTY)
        return std::errc::operation_would_block;

    return error_category::default_error_condition(value);
}

std::error_code asyncio::make_error_code(const TryReceiveError e) {
    return {static_cast<int>(e), zero::Singleton<TryReceiveErrorCategory>::getInstance()};
}

const char *asyncio::ReceiveSyncErrorCategory::name() const noexcept {
    return "asyncio::Receiver::receiveSync";
}

std::string asyncio::ReceiveSyncErrorCategory::message(const int value) const {
    std::string msg;

    switch (static_cast<ReceiveSyncError>(value)) {
    case ReceiveSyncError::DISCONNECTED:
        msg = "channel is empty and disconnected";
        break;

    case ReceiveSyncError::TIMEOUT:
        msg = "timed out waiting on receive operation";
        break;

    default:
        msg = "unknown";
        break;
    }

    return msg;
}

std::error_condition asyncio::ReceiveSyncErrorCategory::default_error_condition(const int value) const noexcept {
    if (static_cast<ReceiveSyncError>(value) == ReceiveSyncError::TIMEOUT)
        return std::errc::timed_out;

    return error_category::default_error_condition(value);
}

std::error_code asyncio::make_error_code(const ReceiveSyncError e) {
    return {static_cast<int>(e), zero::Singleton<ReceiveSyncErrorCategory>::getInstance()};
}

const char *asyncio::ReceiveErrorCategory::name() const noexcept {
    return "asyncio::Receiver::receive";
}

std::string asyncio::ReceiveErrorCategory::message(const int value) const {
    std::string msg;

    switch (static_cast<ReceiveError>(value)) {
    case ReceiveError::DISCONNECTED:
        msg = "channel is empty and disconnected";
        break;

    case ReceiveError::CANCELLED:
        msg = "receive operation has been cancelled";
        break;

    default:
        msg = "unknown";
        break;
    }

    return msg;
}

std::error_condition asyncio::ReceiveErrorCategory::default_error_condition(const int value) const noexcept {
    if (static_cast<ReceiveError>(value) == ReceiveError::CANCELLED)
        return std::errc::operation_canceled;

    return error_category::default_error_condition(value);
}

std::error_code asyncio::make_error_code(const ReceiveError e) {
    return {static_cast<int>(e), zero::Singleton<ReceiveErrorCategory>::getInstance()};
}
