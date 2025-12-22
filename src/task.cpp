#include <asyncio/task.h>
#include <fmt/std.h>
#include <fmt/ranges.h>
#include <stack>

void asyncio::task::Frame::step() {
    children.clear();
    location.reset();
    cancel = nullptr;
}

void asyncio::task::Frame::end() {
    finished = true;

    const auto eventLoop = getEventLoop();

    for (auto &callback: std::exchange(callbacks, {})) {
        eventLoop->post([callback = std::move(callback)] {
            callback();
        });
    }
}

std::expected<void, std::error_code> asyncio::task::Frame::cancelAll() {
    std::list<std::error_code> errors;

    std::stack<Frame *> stack;
    stack.push(this);

    while (!stack.empty()) {
        auto frame = stack.top();
        stack.pop();

        while (true) {
            if (frame->finished) {
                errors.emplace_back(Error::ALREADY_COMPLETED);
                break;
            }

            frame->cancelled = true;

            if (frame->locked) {
                errors.emplace_back(Error::LOCKED);
                break;
            }

            if (frame->cancel) {
                if (const auto result = std::exchange(frame->cancel, nullptr)(); !result)
                    errors.push_back(result.error());

                break;
            }

            if (frame->children.empty()) {
                errors.emplace_back(Error::CANCELLATION_NOT_SUPPORTED);
                break;
            }

            for (const auto &f: frame->children | std::views::drop(1) | std::views::reverse)
                stack.push(f.get());

            frame = frame->children.front().get();
        }
    }

    if (!errors.empty())
        return std::unexpected{errors.back()};

    return {};
}

tree<std::source_location> asyncio::task::Frame::callTree() const {
    tree<std::source_location> tr;
    std::stack<std::pair<tree<std::source_location>::iterator, const Frame *>> stack;

    stack.emplace(tr.begin(), this);

    while (!stack.empty()) {
        auto [it, frame] = stack.top();
        stack.pop();

        while (true) {
            if (frame->finished)
                break;

            assert(frame->location);

            if (tr.empty())
                it = tr.insert(it, *frame->location);
            else
                it = tr.append_child(it, *frame->location);

            if (frame->children.empty())
                break;

            for (const auto &f: frame->children | std::views::drop(1) | std::views::reverse)
                stack.emplace(it, f.get());

            frame = frame->children.front().get();
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

Z_DEFINE_ERROR_CATEGORY_INSTANCE(asyncio::task::Error)
