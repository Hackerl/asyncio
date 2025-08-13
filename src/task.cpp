#include <asyncio/task.h>
#include <fmt/std.h>
#include <fmt/ranges.h>
#include <stack>

void asyncio::task::Frame::step() {
    next.reset();
    location.reset();
    cancel = nullptr;
    group = nullptr;
}

void asyncio::task::Frame::end() {
    finished = true;

    const auto eventLoop = getEventLoop();

    for (auto &callback: std::exchange(callbacks, {})) {
        if (const auto result = eventLoop->post([callback = std::move(callback)] {
            callback();
        }); !result)
            throw std::system_error{result.error()};
    }
}

std::expected<void, std::error_code> asyncio::task::Frame::cancelAll() {
    auto frame = this;

    while (true) {
        frame->cancelled = true;

        if (frame->locked)
            return std::unexpected{Error::LOCKED};

        if (frame->cancel)
            return std::exchange(frame->cancel, nullptr)();

        if (!frame->next)
            return std::unexpected{Error::CANCELLATION_NOT_SUPPORTED};

        frame = frame->next.get();
    }
}

tree<std::source_location> asyncio::task::Frame::callTree() const {
    tree<std::source_location> tr;
    std::stack<std::pair<tree<std::source_location>::iterator, const Frame *>> stack;

    stack.emplace(tr.begin(), this);

    while (!stack.empty()) {
        auto [it, frame] = stack.top();
        stack.pop();

        if (frame->finished)
            continue;

        assert(frame->location);

        while (frame) {
            if (tr.empty())
                it = tr.insert(it, *frame->location);
            else
                it = tr.append_child(it, *frame->location);

            if (!frame->next && frame->group) {
                for (const auto &f: frame->group->mFrames)
                    stack.emplace(it, f.get());

                break;
            }

            frame = frame->next.get();
        }
    }

    return tr;
}

std::string asyncio::task::Frame::trace() const {
    std::vector<std::string> frames;

    const auto tr = callTree();

    for (auto it = tr.begin(); it != tr.end(); ++it) {
        frames.push_back(
            fmt::format(
                "{}{}",
                std::views::repeat(
                    '\t',
                    tree<std::source_location>::depth(it)
                ) | std::ranges::to<std::string>(),
                *it
            )
        );
    }

    return to_string(fmt::join(frames, "\n"));
}

bool asyncio::task::TaskGroup::cancelled() const {
    return mCancelled;
}

std::expected<void, std::error_code> asyncio::task::TaskGroup::cancel() {
    mCancelled = true;

    const auto errors = mFrames
        | std::views::filter([](const auto &frame) {
            return !frame->finished;
        })
        | std::views::transform([](const auto &frame) {
            return frame->cancelAll();
        })
        | std::views::filter([](const auto &result) {
            return !result;
        })
        | std::ranges::views::transform([](const auto &result) {
            return result.error();
        })
        | std::ranges::to<std::list>();

    if (!errors.empty())
        return std::unexpected{errors.back()};

    return {};
}

DEFINE_ERROR_CATEGORY_INSTANCE(asyncio::task::Error)
