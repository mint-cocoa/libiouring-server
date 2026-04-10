#include <serverweb/Middleware.h>

namespace serverweb {

void Pipeline::Use(MiddlewareRef mw) {
    middlewares_.push_back(std::move(mw));
}

void Pipeline::Execute(RequestContext& ctx, std::function<void()> final_handler) const {
    if (middlewares_.empty()) {
        final_handler();
        return;
    }

    // Build chain in reverse: last middleware wraps final_handler,
    // each earlier middleware wraps the next one.
    std::function<void()> chain = std::move(final_handler);

    for (auto it = middlewares_.rbegin(); it != middlewares_.rend(); ++it) {
        auto& mw = *it;
        chain = [&ctx, &mw, next = std::move(chain)]() {
            mw->Process(ctx, next);
        };
    }

    chain();
}

} // namespace serverweb
