#include <serverweb/RadixTree.h>

namespace serverweb {

RadixTree::RadixTree()
    : root_(std::make_unique<Node>()) {}

RadixTree::~RadixTree() = default;

RadixTree::RadixTree(RadixTree&&) noexcept = default;
RadixTree& RadixTree::operator=(RadixTree&&) noexcept = default;

std::vector<std::string> RadixTree::SplitPath(const std::string& path) {
    std::vector<std::string> segments;
    std::size_t start = 0;
    // Skip leading slash
    if (!path.empty() && path[0] == '/') start = 1;

    while (start < path.size()) {
        auto pos = path.find('/', start);
        if (pos == std::string::npos) {
            segments.push_back(path.substr(start));
            break;
        }
        if (pos > start) {
            segments.push_back(path.substr(start, pos - start));
        }
        start = pos + 1;
    }
    return segments;
}

std::vector<std::string_view> RadixTree::SplitPathView(std::string_view path) {
    std::vector<std::string_view> segments;
    std::size_t start = 0;
    if (!path.empty() && path[0] == '/') start = 1;

    while (start < path.size()) {
        auto pos = path.find('/', start);
        if (pos == std::string_view::npos) {
            segments.push_back(path.substr(start));
            break;
        }
        if (pos > start) {
            segments.push_back(path.substr(start, pos - start));
        }
        start = pos + 1;
    }

    // Trailing slash produces a distinct path (e.g., "/users/" != "/users")
    if (path.size() > 1 && path.back() == '/') {
        segments.emplace_back();  // empty sentinel segment
    }

    return segments;
}

void RadixTree::Insert(HttpMethod method, const std::string& path, HttpHandler handler) {
    if (path == "/") {
        // Root path handler
        root_->handlers[static_cast<std::size_t>(method)] = std::move(handler);
        return;
    }

    auto segments = SplitPath(path);
    InsertSegments(root_.get(), segments, 0, method, std::move(handler));
}

void RadixTree::InsertSegments(Node* node, const std::vector<std::string>& segments,
                               std::size_t idx, HttpMethod method, HttpHandler handler) {
    if (idx >= segments.size()) {
        node->handlers[static_cast<std::size_t>(method)] = std::move(handler);
        return;
    }

    const auto& seg = segments[idx];

    // Wildcard segment: *name -- captures rest of path
    if (seg.size() > 1 && seg[0] == '*') {
        if (!node->wildcard_child) {
            node->wildcard_child = std::make_unique<Node>();
            node->wildcard_name = seg.substr(1);
        }
        // Wildcard must be the last segment
        node->wildcard_child->handlers[static_cast<std::size_t>(method)] = std::move(handler);
        return;
    }

    // Parameter segment: :name
    if (seg.size() > 1 && seg[0] == ':') {
        if (!node->param_child) {
            node->param_child = std::make_unique<Node>();
            node->param_name = seg.substr(1);
        }
        InsertSegments(node->param_child.get(), segments, idx + 1, method, std::move(handler));
        return;
    }

    // Static segment -- find existing child with matching prefix
    for (auto& child : node->children) {
        if (child->prefix == seg) {
            InsertSegments(child.get(), segments, idx + 1, method, std::move(handler));
            return;
        }
    }

    // No matching child, create new one
    auto child = std::make_unique<Node>();
    child->prefix = seg;
    auto* raw = child.get();
    node->children.push_back(std::move(child));
    InsertSegments(raw, segments, idx + 1, method, std::move(handler));
}

MatchResult RadixTree::Match(HttpMethod method, std::string_view path) const {
    MatchResult result;

    if (path == "/") {
        // Root path
        auto& handler = root_->handlers[static_cast<std::size_t>(method)];
        if (handler) {
            result.handler = const_cast<HttpHandler*>(&handler);
            result.path_exists = true;
        } else if (root_->HasAnyHandler()) {
            result.path_exists = true;
        }
        return result;
    }

    auto segments = SplitPathView(path);
    std::vector<std::pair<std::string_view, std::string_view>> params;
    MatchNode(root_.get(), segments, 0, method, result, params);
    return result;
}

void RadixTree::MatchNode(const Node* node, const std::vector<std::string_view>& segments,
                          std::size_t idx, HttpMethod method, MatchResult& result,
                          std::vector<std::pair<std::string_view, std::string_view>>& params) const {
    if (idx >= segments.size()) {
        // We've consumed all segments -- check this node for handlers
        auto& handler = node->handlers[static_cast<std::size_t>(method)];
        if (handler) {
            result.handler = const_cast<HttpHandler*>(&handler);
            result.params = params;
            result.path_exists = true;
        } else if (node->HasAnyHandler()) {
            result.path_exists = true;
        }
        return;
    }

    // Already found a handler (from a higher-priority match) -- stop
    if (result.handler) return;

    auto seg = segments[idx];

    // Priority 1: Static children (exact match)
    for (auto& child : node->children) {
        if (child->prefix == seg) {
            MatchNode(child.get(), segments, idx + 1, method, result, params);
            if (result.handler) return;
            break;
        }
    }

    // Priority 2: Parameter child
    if (node->param_child && !result.handler) {
        params.emplace_back(std::string_view(node->param_name), seg);
        MatchNode(node->param_child.get(), segments, idx + 1, method, result, params);
        if (result.handler) return;
        params.pop_back();
    }

    // Priority 3: Wildcard child -- captures remaining path
    if (node->wildcard_child && !result.handler) {
        // Reconstruct remaining path from segments[idx..]
        // The wildcard captures the rest of the original path
        auto first = segments[idx];
        auto last = segments.back();
        // Calculate the full span from first segment to end of last segment
        std::string_view wildcard_value(first.data(),
            static_cast<std::size_t>(last.data() + last.size() - first.data()));

        auto& wc_handler = node->wildcard_child->handlers[static_cast<std::size_t>(method)];
        if (wc_handler) {
            params.emplace_back(std::string_view(node->wildcard_name), wildcard_value);
            result.handler = const_cast<HttpHandler*>(&wc_handler);
            result.params = params;
            result.path_exists = true;
        } else if (node->wildcard_child->HasAnyHandler()) {
            result.path_exists = true;
        }
    }
}

} // namespace serverweb
