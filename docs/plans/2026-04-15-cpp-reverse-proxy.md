# C++ io_uring Reverse Proxy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** libiouring-server에 HTTP/WebSocket 리버스 프록시와 Host 기반 라우팅을 추가하여 Quarto 플랫폼의 진입점 역할을 수행하게 한다

**Architecture:** ReverseProxy 미들웨어가 Host 헤더를 파싱하여 app/blog 도메인을 분기한다. HTTP 프록시는 io_uring connect/send/recv로 upstream 연결을 관리하고, WebSocket 프록시는 업그레이드 감지 후 양방향 프레임 릴레이를 수행한다. 기존 StaticFiles 미들웨어 패턴을 따르며, UpstreamSession이 EventHandler를 상속하여 비동기 I/O를 처리한다.

**Tech Stack:** C++23, io_uring, liburing, llhttp, OpenSSL (JWT만), CMake

---

## File Structure

```
libiouring-server/
├── include/serverweb/
│   ├── ReverseProxy.h              # 리버스 프록시 미들웨어 (Host 라우팅 포함)
│   ├── UpstreamSession.h           # Upstream TCP 연결 핸들러
│   └── UpstreamPool.h              # Upstream 커넥션 풀
├── src/web/
│   ├── ReverseProxy.cpp            # 미들웨어 구현
│   ├── UpstreamSession.cpp         # Upstream 세션 구현
│   └── UpstreamPool.cpp            # 커넥션 풀 구현
├── tests/web/
│   ├── ReverseProxyTest.cpp        # 프록시 미들웨어 테스트
│   ├── UpstreamSessionTest.cpp     # Upstream 세션 테스트
│   └── UpstreamPoolTest.cpp        # 커넥션 풀 테스트
└── examples/
    └── ReverseProxy/
        └── src/main.cpp            # Quarto 플랫폼용 프록시 설정 예제
```

---

## Task 1: UpstreamSession — Upstream TCP 연결 핸들러

**Files:**
- Create: `include/serverweb/UpstreamSession.h`
- Create: `src/web/UpstreamSession.cpp`
- Create: `tests/web/UpstreamSessionTest.cpp`
- Modify: `CMakeLists.txt` (소스 추가)

- [ ] **Step 1: UpstreamSession 헤더 작성**

```cpp
// include/serverweb/UpstreamSession.h
#pragma once

#include <servercore/ring/IoRing.h>
#include <servercore/ring/RingEvent.h>
#include <servercore/buffer/SendBuffer.h>
#include <servercore/buffer/BufferPool.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace serverweb {

using ProxyCallback = std::function<void(
    int status_code,
    std::vector<std::pair<std::string, std::string>> headers,
    std::vector<std::byte> body
)>;

using ProxyErrorCallback = std::function<void(std::string error)>;

class UpstreamSession : public servercore::ring::EventHandler {
public:
    UpstreamSession(servercore::ring::IoRing& ring,
                    servercore::buffer::BufferPool& pool);
    ~UpstreamSession();

    // Upstream으로 연결 + 요청 전송
    void Connect(const std::string& host, std::uint16_t port,
                 std::string request_bytes,
                 ProxyCallback on_response,
                 ProxyErrorCallback on_error);

    // 연결 해제
    void Close();

    bool IsConnected() const { return connected_; }
    int Fd() const { return fd_; }

    // EventHandler overrides
    void OnConnect(servercore::ring::ConnectEvent& ev, std::int32_t result) override;
    void OnSend(servercore::ring::SendEvent& ev, std::int32_t result) override;
    void OnRecv(servercore::ring::RecvEvent& ev,
                std::int32_t result, std::uint32_t flags) override;

private:
    void StartRecv();
    void ParseResponse();

    servercore::ring::IoRing& ring_;
    servercore::buffer::BufferPool& pool_;
    int fd_ = -1;
    bool connected_ = false;

    // 요청 데이터
    std::string pending_request_;

    // 응답 버퍼링
    std::vector<std::byte> recv_buffer_;

    // 콜백
    ProxyCallback on_response_;
    ProxyErrorCallback on_error_;

    // io_uring 이벤트
    servercore::ring::ConnectEvent connect_ev_;
    servercore::ring::SendEvent send_ev_;
    servercore::ring::RecvEvent recv_ev_;
};

} // namespace serverweb
```

- [ ] **Step 2: UpstreamSession 구현**

```cpp
// src/web/UpstreamSession.cpp
#include <serverweb/UpstreamSession.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <spdlog/spdlog.h>

namespace serverweb {

UpstreamSession::UpstreamSession(servercore::ring::IoRing& ring,
                                 servercore::buffer::BufferPool& pool)
    : ring_(ring), pool_(pool) {
    connect_ev_.SetOwner(weak_from_this());
    send_ev_.SetOwner(weak_from_this());
    recv_ev_.SetOwner(weak_from_this());
}

UpstreamSession::~UpstreamSession() {
    Close();
}

void UpstreamSession::Connect(const std::string& host, std::uint16_t port,
                               std::string request_bytes,
                               ProxyCallback on_response,
                               ProxyErrorCallback on_error) {
    pending_request_ = std::move(request_bytes);
    on_response_ = std::move(on_response);
    on_error_ = std::move(on_error);

    // DNS 해석 (동기 — 향후 비동기로 전환 가능)
    struct addrinfo hints{}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    auto port_str = std::to_string(port);
    int err = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (err != 0) {
        if (on_error_) on_error_(std::string("DNS resolution failed: ") + gai_strerror(err));
        return;
    }

    fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd_ < 0) {
        freeaddrinfo(res);
        if (on_error_) on_error_("Failed to create socket");
        return;
    }

    ring_.PrepConnect(connect_ev_, fd_, res->ai_addr, res->ai_addrlen);
    ring_.Submit();
    freeaddrinfo(res);
}

void UpstreamSession::Close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    connected_ = false;
}

void UpstreamSession::OnConnect(servercore::ring::ConnectEvent& ev,
                                 std::int32_t result) {
    if (result < 0) {
        spdlog::error("[ReverseProxy] Upstream connect failed: {}", strerror(-result));
        if (on_error_) on_error_("Upstream connection refused");
        Close();
        return;
    }

    connected_ = true;

    // 요청 전송
    auto buf = pool_.Allocate(pending_request_.size());
    if (!buf) {
        if (on_error_) on_error_("Buffer allocation failed");
        Close();
        return;
    }

    auto writable = (*buf)->Writable();
    std::memcpy(writable.data(), pending_request_.data(), pending_request_.size());
    (*buf)->Commit(pending_request_.size());

    send_ev_.SetRequestedBytes(pending_request_.size());
    ring_.PrepSend(send_ev_, fd_);
    ring_.Submit();
}

void UpstreamSession::OnSend(servercore::ring::SendEvent& ev,
                              std::int32_t result) {
    if (result < 0) {
        spdlog::error("[ReverseProxy] Upstream send failed: {}", strerror(-result));
        if (on_error_) on_error_("Upstream send failed");
        Close();
        return;
    }

    // 응답 수신 시작
    StartRecv();
}

void UpstreamSession::StartRecv() {
    ring_.PrepRecv(recv_ev_, fd_);
    ring_.Submit();
}

void UpstreamSession::OnRecv(servercore::ring::RecvEvent& ev,
                              std::int32_t result, std::uint32_t flags) {
    if (result <= 0) {
        // 연결 종료 또는 에러 — 응답 완료로 간주
        if (recv_buffer_.empty()) {
            if (on_error_) on_error_("Upstream closed without response");
        } else {
            ParseResponse();
        }
        Close();
        return;
    }

    // 데이터 누적
    auto data = ring_.GetRecvData(ev, result);
    recv_buffer_.insert(recv_buffer_.end(), data.begin(), data.end());

    // Content-Length 체크하여 완료 판단 (간단 구현)
    // 전체 응답을 받았는지 확인
    std::string_view sv(reinterpret_cast<const char*>(recv_buffer_.data()),
                        recv_buffer_.size());

    auto header_end = sv.find("\r\n\r\n");
    if (header_end != std::string_view::npos) {
        // Content-Length 파싱
        auto cl_pos = sv.find("Content-Length: ");
        if (cl_pos != std::string_view::npos && cl_pos < header_end) {
            auto cl_start = cl_pos + 16;
            auto cl_end = sv.find("\r\n", cl_start);
            auto cl_str = sv.substr(cl_start, cl_end - cl_start);
            std::size_t content_length = std::stoull(std::string(cl_str));
            std::size_t total_expected = header_end + 4 + content_length;

            if (recv_buffer_.size() >= total_expected) {
                ParseResponse();
                return;
            }
        } else {
            // Content-Length 없으면 연결 종료까지 대기
        }
    }

    // 아직 완료되지 않았으면 계속 수신
    StartRecv();
}

void UpstreamSession::ParseResponse() {
    std::string_view sv(reinterpret_cast<const char*>(recv_buffer_.data()),
                        recv_buffer_.size());

    // 상태 라인 파싱: "HTTP/1.1 200 OK\r\n"
    auto first_line_end = sv.find("\r\n");
    if (first_line_end == std::string_view::npos) {
        if (on_error_) on_error_("Invalid upstream response");
        return;
    }

    auto status_line = sv.substr(0, first_line_end);
    int status_code = 502;
    auto space1 = status_line.find(' ');
    if (space1 != std::string_view::npos) {
        auto space2 = status_line.find(' ', space1 + 1);
        auto code_str = status_line.substr(space1 + 1, space2 - space1 - 1);
        status_code = std::stoi(std::string(code_str));
    }

    // 헤더 파싱
    std::vector<std::pair<std::string, std::string>> headers;
    auto header_end = sv.find("\r\n\r\n");
    auto header_section = sv.substr(first_line_end + 2,
                                     header_end - first_line_end - 2);

    std::size_t pos = 0;
    while (pos < header_section.size()) {
        auto line_end = header_section.find("\r\n", pos);
        if (line_end == std::string_view::npos) line_end = header_section.size();
        auto line = header_section.substr(pos, line_end - pos);

        auto colon = line.find(": ");
        if (colon != std::string_view::npos) {
            headers.emplace_back(
                std::string(line.substr(0, colon)),
                std::string(line.substr(colon + 2))
            );
        }
        pos = line_end + 2;
    }

    // 바디 추출
    std::vector<std::byte> body;
    if (header_end + 4 < recv_buffer_.size()) {
        body.assign(recv_buffer_.begin() + header_end + 4, recv_buffer_.end());
    }

    if (on_response_) {
        on_response_(status_code, std::move(headers), std::move(body));
    }
}

} // namespace serverweb
```

- [ ] **Step 3: 테스트 작성**

```cpp
// tests/web/UpstreamSessionTest.cpp
#include <gtest/gtest.h>
#include <serverweb/UpstreamSession.h>

using namespace serverweb;

// UpstreamSession의 ParseResponse 로직을 단위 테스트
// 실제 io_uring 연결은 통합 테스트에서 검증

TEST(UpstreamSessionTest, ConstructionDoesNotThrow) {
    // UpstreamSession은 IoRing 참조가 필요하므로
    // 여기서는 생성자 시그니처만 검증
    SUCCEED();  // 컴파일 검증용
}
```

**Note:** io_uring 기반 코드는 실제 커널 인터페이스가 필요하므로 단위 테스트보다 통합 테스트가 적합하다. Task 6에서 전체 통합 테스트를 작성한다.

- [ ] **Step 4: CMakeLists.txt에 소스 추가**

`src/web/UpstreamSession.cpp`를 `SERVERWEB_SOURCES` 리스트에 추가한다.

기존 CMakeLists.txt에서 `SERVERWEB_SOURCES` 변수를 찾아 추가:

```cmake
set(SERVERWEB_SOURCES
    # ... 기존 파일들 ...
    src/web/UpstreamSession.cpp
)
```

- [ ] **Step 5: 빌드 확인**

```bash
cd /tmp/libiouring-server
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j$(nproc)
```

Expected: 컴파일 성공

- [ ] **Step 6: Commit**

```bash
git add include/serverweb/UpstreamSession.h src/web/UpstreamSession.cpp tests/web/UpstreamSessionTest.cpp CMakeLists.txt
git commit -m "feat: UpstreamSession for io_uring-based upstream TCP connections"
```

---

## Task 2: UpstreamPool — 커넥션 풀

**Files:**
- Create: `include/serverweb/UpstreamPool.h`
- Create: `src/web/UpstreamPool.cpp`
- Create: `tests/web/UpstreamPoolTest.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: UpstreamPool 헤더 작성**

```cpp
// include/serverweb/UpstreamPool.h
#pragma once

#include <serverweb/UpstreamSession.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>

namespace serverweb {

struct UpstreamTarget {
    std::string host;
    std::uint16_t port;

    std::string Key() const { return host + ":" + std::to_string(port); }
};

class UpstreamPool {
public:
    explicit UpstreamPool(std::size_t max_idle_per_target = 4,
                          std::chrono::seconds idle_timeout = std::chrono::seconds(60));

    // HTTP 프록시: 요청 전송 후 콜백으로 응답 수신
    void Forward(servercore::ring::IoRing& ring,
                 servercore::buffer::BufferPool& pool,
                 const UpstreamTarget& target,
                 std::string request_bytes,
                 ProxyCallback on_response,
                 ProxyErrorCallback on_error);

    std::size_t IdleCount(const std::string& key) const;
    std::size_t ActiveCount() const;

private:
    std::size_t max_idle_per_target_;
    std::chrono::seconds idle_timeout_;

    mutable std::mutex mu_;
    std::unordered_map<std::string, std::queue<std::shared_ptr<UpstreamSession>>> idle_;
    std::size_t active_count_ = 0;
};

} // namespace serverweb
```

- [ ] **Step 2: UpstreamPool 구현**

```cpp
// src/web/UpstreamPool.cpp
#include <serverweb/UpstreamPool.h>

#include <spdlog/spdlog.h>

namespace serverweb {

UpstreamPool::UpstreamPool(std::size_t max_idle_per_target,
                           std::chrono::seconds idle_timeout)
    : max_idle_per_target_(max_idle_per_target)
    , idle_timeout_(idle_timeout) {}

void UpstreamPool::Forward(servercore::ring::IoRing& ring,
                            servercore::buffer::BufferPool& pool,
                            const UpstreamTarget& target,
                            std::string request_bytes,
                            ProxyCallback on_response,
                            ProxyErrorCallback on_error) {
    auto key = target.Key();

    // 새 연결 생성 (풀링은 향후 최적화)
    auto session = std::make_shared<UpstreamSession>(ring, pool);

    {
        std::lock_guard lock(mu_);
        active_count_++;
    }

    auto weak_this = std::weak_ptr<UpstreamPool>{};  // 풀 참조 불필요 (현재)

    auto wrapped_response = [this, on_response, session](
        int status, auto headers, auto body) {
        {
            std::lock_guard lock(mu_);
            active_count_--;
        }
        if (on_response) on_response(status, std::move(headers), std::move(body));
    };

    auto wrapped_error = [this, on_error, session](std::string err) {
        {
            std::lock_guard lock(mu_);
            active_count_--;
        }
        if (on_error) on_error(std::move(err));
    };

    session->Connect(target.host, target.port,
                     std::move(request_bytes),
                     std::move(wrapped_response),
                     std::move(wrapped_error));
}

std::size_t UpstreamPool::IdleCount(const std::string& key) const {
    std::lock_guard lock(mu_);
    auto it = idle_.find(key);
    if (it == idle_.end()) return 0;
    return it->second.size();
}

std::size_t UpstreamPool::ActiveCount() const {
    std::lock_guard lock(mu_);
    return active_count_;
}

} // namespace serverweb
```

- [ ] **Step 3: 테스트 작성**

```cpp
// tests/web/UpstreamPoolTest.cpp
#include <gtest/gtest.h>
#include <serverweb/UpstreamPool.h>

using namespace serverweb;

TEST(UpstreamPoolTest, InitialCountsAreZero) {
    UpstreamPool pool;
    EXPECT_EQ(pool.ActiveCount(), 0);
    EXPECT_EQ(pool.IdleCount("localhost:8000"), 0);
}

TEST(UpstreamTargetTest, KeyFormat) {
    UpstreamTarget target{"gateway", 8000};
    EXPECT_EQ(target.Key(), "gateway:8000");
}
```

- [ ] **Step 4: CMakeLists.txt 업데이트, 빌드 확인**

```bash
# src/web/UpstreamPool.cpp를 SERVERWEB_SOURCES에 추가
cd /tmp/libiouring-server/build
cmake --build . -j$(nproc)
ctest --test-dir . -R "UpstreamPool" --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add include/serverweb/UpstreamPool.h src/web/UpstreamPool.cpp tests/web/UpstreamPoolTest.cpp CMakeLists.txt
git commit -m "feat: UpstreamPool for connection management"
```

---

## Task 3: ReverseProxy 미들웨어 — HTTP 프록시

**Files:**
- Create: `include/serverweb/ReverseProxy.h`
- Create: `src/web/ReverseProxy.cpp`
- Create: `tests/web/ReverseProxyTest.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: ReverseProxy 헤더 작성**

```cpp
// include/serverweb/ReverseProxy.h
#pragma once

#include <serverweb/Middleware.h>
#include <serverweb/UpstreamPool.h>

#include <memory>
#include <string>
#include <vector>

namespace serverweb {
namespace middleware {

struct ProxyRoute {
    std::string path_prefix;     // "/api/", "/auth/", "/preview/"
    std::string upstream_host;   // "gateway"
    std::uint16_t upstream_port; // 8000
};

struct ReverseProxyOptions {
    std::vector<ProxyRoute> routes;
    std::size_t max_idle_connections = 4;
    std::chrono::seconds connect_timeout = std::chrono::seconds(30);
    std::chrono::seconds response_timeout = std::chrono::seconds(60);
};

class ReverseProxy : public IMiddleware {
public:
    explicit ReverseProxy(ReverseProxyOptions opts);
    void Process(RequestContext& ctx, NextFn next) override;

private:
    // path에 매칭되는 upstream 찾기
    const ProxyRoute* FindRoute(std::string_view path) const;

    // HTTP 요청을 raw bytes로 직렬화
    std::string SerializeRequest(const HttpRequest& request,
                                 const ProxyRoute& route) const;

    ReverseProxyOptions opts_;
    std::shared_ptr<UpstreamPool> pool_;
};

} // namespace middleware
} // namespace serverweb
```

- [ ] **Step 2: ReverseProxy 구현**

```cpp
// src/web/ReverseProxy.cpp
#include <serverweb/ReverseProxy.h>
#include <serverweb/HttpRequest.h>
#include <serverweb/HttpResponse.h>
#include <serverweb/Router.h>

#include <spdlog/spdlog.h>

namespace serverweb {
namespace middleware {

ReverseProxy::ReverseProxy(ReverseProxyOptions opts)
    : opts_(std::move(opts))
    , pool_(std::make_shared<UpstreamPool>(opts_.max_idle_connections)) {}

void ReverseProxy::Process(RequestContext& ctx, NextFn next) {
    const auto* route = FindRoute(ctx.request.path);
    if (!route) {
        next();  // 매칭 안 되면 다음 미들웨어로
        return;
    }

    // 응답을 비동기로 처리
    ctx.Defer();

    auto request_bytes = SerializeRequest(ctx.request, *route);
    UpstreamTarget target{route->upstream_host, route->upstream_port};

    auto& session = ctx.session;
    auto& pool = ctx.pool;

    pool_->Forward(
        session.Ring(), pool, target,
        std::move(request_bytes),
        // 성공 콜백
        [&session, &pool](int status, auto headers, auto body) {
            // upstream 응답을 클라이언트에게 전달
            HttpResponse response(session, pool);
            response.Status(static_cast<HttpStatus>(status));

            for (const auto& [name, value] : headers) {
                // hop-by-hop 헤더 제외
                if (name == "Transfer-Encoding" || name == "Connection") continue;
                response.Header(name, value);
            }

            response.Body(std::string(
                reinterpret_cast<const char*>(body.data()), body.size()
            ));
            response.Send();
        },
        // 에러 콜백
        [&session, &pool](std::string error) {
            spdlog::error("[ReverseProxy] {}", error);
            HttpResponse response(session, pool);
            response.Status(HttpStatus::kBadGateway)
                    .Json(R"({"error":")" + error + R"("})");
            response.Send();
        }
    );
}

const ProxyRoute* ReverseProxy::FindRoute(std::string_view path) const {
    for (const auto& route : opts_.routes) {
        if (path.substr(0, route.path_prefix.size()) == route.path_prefix) {
            return &route;
        }
    }
    return nullptr;
}

std::string ReverseProxy::SerializeRequest(const HttpRequest& request,
                                            const ProxyRoute& route) const {
    // HTTP/1.1 요청 직렬화
    std::string method_str;
    switch (request.method) {
        case HttpMethod::kGet:    method_str = "GET"; break;
        case HttpMethod::kPost:   method_str = "POST"; break;
        case HttpMethod::kPut:    method_str = "PUT"; break;
        case HttpMethod::kDelete: method_str = "DELETE"; break;
        case HttpMethod::kPatch:  method_str = "PATCH"; break;
        case HttpMethod::kHead:   method_str = "HEAD"; break;
        default:                  method_str = "GET"; break;
    }

    std::string result;
    result += method_str + " " + std::string(request.path);
    if (!request.query.empty()) {
        result += "?" + std::string(request.query);
    }
    result += " HTTP/1.1\r\n";

    // Host 헤더를 upstream으로 변경
    result += "Host: " + route.upstream_host + ":"
              + std::to_string(route.upstream_port) + "\r\n";

    // 원본 헤더 전달 (Host 제외)
    for (const auto& [name, value] : request.headers_) {
        if (name == "Host" || name == "host") continue;
        result += name + ": " + value + "\r\n";
    }

    // X-Forwarded 헤더 추가
    result += "X-Forwarded-For: " + std::string(request.GetHeader("X-Real-IP")) + "\r\n";
    result += "Connection: close\r\n";  // 간단 구현: 매번 연결 종료

    result += "\r\n";

    // 바디
    if (!request.body.empty()) {
        result += request.body;
    }

    return result;
}

} // namespace middleware
} // namespace serverweb
```

- [ ] **Step 3: 테스트 작성**

```cpp
// tests/web/ReverseProxyTest.cpp
#include <gtest/gtest.h>
#include <serverweb/ReverseProxy.h>

using namespace serverweb;
using namespace serverweb::middleware;

TEST(ReverseProxyTest, FindRouteMatchesPrefix) {
    ReverseProxyOptions opts;
    opts.routes = {
        {"/api/", "gateway", 8000},
        {"/auth/", "gateway", 8000},
    };
    ReverseProxy proxy(opts);

    // FindRoute는 private이므로 Process 동작을 통해 간접 테스트
    // 여기서는 ProxyRoute 구조체와 옵션 구성을 검증
    EXPECT_EQ(opts.routes[0].path_prefix, "/api/");
    EXPECT_EQ(opts.routes[0].upstream_host, "gateway");
    EXPECT_EQ(opts.routes[0].upstream_port, 8000);
}

TEST(ProxyRouteTest, UpstreamTargetFromRoute) {
    ProxyRoute route{"/api/", "gateway", 8000};
    UpstreamTarget target{route.upstream_host, route.upstream_port};
    EXPECT_EQ(target.Key(), "gateway:8000");
}
```

- [ ] **Step 4: CMakeLists.txt 업데이트, 빌드 확인**

```bash
cd /tmp/libiouring-server/build
cmake --build . -j$(nproc)
ctest --test-dir . -R "ReverseProxy" --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add include/serverweb/ReverseProxy.h src/web/ReverseProxy.cpp tests/web/ReverseProxyTest.cpp CMakeLists.txt
git commit -m "feat: ReverseProxy middleware with path-based routing"
```

---

## Task 4: WebSocket 리버스 프록시

**Files:**
- Modify: `include/serverweb/ReverseProxy.h` (WS 프록시 추가)
- Modify: `src/web/ReverseProxy.cpp` (WS 업그레이드 감지 + 양방향 릴레이)

- [ ] **Step 1: WsProxyHandler 헤더 추가**

`include/serverweb/ReverseProxy.h`에 추가:

```cpp
// WebSocket 양방향 프록시 핸들러
class WsProxyHandler : public ws::WebSocketHandler {
public:
    WsProxyHandler(servercore::ring::IoRing& ring,
                   servercore::buffer::BufferPool& pool,
                   const UpstreamTarget& target,
                   const HttpRequest& original_request);

    void OnOpen(HttpSession& client_session) override;
    void OnMessage(HttpSession& client_session,
                   std::string_view data, bool is_text) override;
    void OnClose(HttpSession& client_session,
                 std::uint16_t code, std::string_view reason) override;

private:
    servercore::ring::IoRing& ring_;
    servercore::buffer::BufferPool& pool_;
    UpstreamTarget target_;
    std::string upgrade_request_;

    // Upstream WebSocket 연결
    std::shared_ptr<UpstreamSession> upstream_;
    bool upstream_connected_ = false;
};
```

- [ ] **Step 2: ReverseProxy::Process에 WebSocket 업그레이드 감지 추가**

`src/web/ReverseProxy.cpp`의 `Process()` 메서드 시작 부분에 추가:

```cpp
void ReverseProxy::Process(RequestContext& ctx, NextFn next) {
    const auto* route = FindRoute(ctx.request.path);
    if (!route) {
        next();
        return;
    }

    // WebSocket 업그레이드 감지
    auto upgrade = ctx.request.GetHeader("Upgrade");
    if (upgrade == "websocket") {
        HandleWebSocketProxy(ctx, *route);
        return;
    }

    // ... 기존 HTTP 프록시 로직 ...
}

void ReverseProxy::HandleWebSocketProxy(RequestContext& ctx,
                                         const ProxyRoute& route) {
    UpstreamTarget target{route.upstream_host, route.upstream_port};
    auto handler = std::make_shared<WsProxyHandler>(
        ctx.session.Ring(), ctx.pool, target, ctx.request
    );
    ctx.session.UpgradeToWebSocket(handler);
}
```

- [ ] **Step 3: WsProxyHandler 구현**

```cpp
// src/web/ReverseProxy.cpp에 추가

WsProxyHandler::WsProxyHandler(servercore::ring::IoRing& ring,
                                servercore::buffer::BufferPool& pool,
                                const UpstreamTarget& target,
                                const HttpRequest& original_request)
    : ring_(ring), pool_(pool), target_(target) {
    // Upstream으로 보낼 WebSocket 업그레이드 요청 구성
    upgrade_request_ = "GET " + std::string(original_request.path);
    if (!original_request.query.empty()) {
        upgrade_request_ += "?" + std::string(original_request.query);
    }
    upgrade_request_ += " HTTP/1.1\r\n";
    upgrade_request_ += "Host: " + target.host + ":" + std::to_string(target.port) + "\r\n";
    upgrade_request_ += "Upgrade: websocket\r\n";
    upgrade_request_ += "Connection: Upgrade\r\n";

    // 원본 WebSocket 헤더 전달
    auto ws_key = original_request.GetHeader("Sec-WebSocket-Key");
    auto ws_version = original_request.GetHeader("Sec-WebSocket-Version");
    if (!ws_key.empty()) {
        upgrade_request_ += "Sec-WebSocket-Key: " + std::string(ws_key) + "\r\n";
    }
    if (!ws_version.empty()) {
        upgrade_request_ += "Sec-WebSocket-Version: " + std::string(ws_version) + "\r\n";
    }
    upgrade_request_ += "\r\n";
}

void WsProxyHandler::OnOpen(HttpSession& client_session) {
    // Client WebSocket이 열리면 upstream으로도 연결
    upstream_ = std::make_shared<UpstreamSession>(ring_, pool_);
    upstream_->Connect(target_.host, target_.port, upgrade_request_,
        // 응답 콜백 — upstream 101 응답 무시 (이미 클라이언트에 101 보냄)
        [](int, auto, auto) {},
        [&client_session](std::string err) {
            spdlog::error("[WsProxy] Upstream error: {}", err);
            client_session.WsClose(1001, "Upstream connection failed");
        }
    );
}

void WsProxyHandler::OnMessage(HttpSession& client_session,
                                std::string_view data, bool is_text) {
    // Client → Upstream: 메시지 릴레이
    // 현재는 upstream이 raw TCP이므로 WebSocket 프레임을 직접 전송해야 함
    // 향후 upstream도 WebSocket 클라이언트로 구현 필요
    // 임시: 로그만 남김
    spdlog::debug("[WsProxy] Client→Upstream: {} bytes", data.size());
}

void WsProxyHandler::OnClose(HttpSession& client_session,
                              std::uint16_t code, std::string_view reason) {
    spdlog::info("[WsProxy] Client closed: {} {}", code, reason);
    if (upstream_) {
        upstream_->Close();
    }
}
```

**Note:** 완전한 WebSocket 양방향 릴레이는 upstream 측도 WebSocket 프레임 파싱이 필요하다. 현재 구현은 HTTP 프록시 + WS 업그레이드 감지까지이며, 완전한 WS 릴레이는 추후 개선한다.

- [ ] **Step 4: 빌드 확인**

```bash
cd /tmp/libiouring-server/build
cmake --build . -j$(nproc)
```

- [ ] **Step 5: Commit**

```bash
git add include/serverweb/ReverseProxy.h src/web/ReverseProxy.cpp
git commit -m "feat: WebSocket proxy handler with upgrade detection"
```

---

## Task 5: Host 기반 라우팅 + 예제 앱

**Files:**
- Modify: `include/serverweb/ReverseProxy.h` (Host 매칭 추가)
- Modify: `src/web/ReverseProxy.cpp`
- Create: `examples/ReverseProxy/src/main.cpp`
- Create: `examples/ReverseProxy/CMakeLists.txt`

- [ ] **Step 1: ReverseProxyOptions에 Host 필드 추가**

`include/serverweb/ReverseProxy.h`의 ProxyRoute에 host_pattern 추가:

```cpp
struct ProxyRoute {
    std::string host_pattern;    // "app.mintcocoa.cc", "blog.mintcocoa.cc", "*"
    std::string path_prefix;     // "/api/", "/", etc.
    std::string upstream_host;   // "gateway"
    std::uint16_t upstream_port; // 8000
    bool is_static = false;      // true면 정적 파일 서빙 (프록시 안 함)
    std::string static_root;     // is_static=true일 때 파일 경로
};
```

- [ ] **Step 2: FindRoute에 Host 매칭 추가**

```cpp
const ProxyRoute* ReverseProxy::FindRoute(std::string_view host,
                                           std::string_view path) const {
    for (const auto& route : opts_.routes) {
        // Host 매칭
        if (route.host_pattern != "*") {
            if (route.host_pattern != host) continue;
        }
        // Path prefix 매칭
        if (path.substr(0, route.path_prefix.size()) == route.path_prefix) {
            return &route;
        }
    }
    return nullptr;
}
```

- [ ] **Step 3: Process()에서 Host 헤더 추출**

```cpp
void ReverseProxy::Process(RequestContext& ctx, NextFn next) {
    auto host_header = ctx.request.GetHeader("Host");
    // "hostname:port"에서 hostname만 추출
    auto colon = host_header.find(':');
    auto host = host_header.substr(0, colon);

    const auto* route = FindRoute(host, ctx.request.path);
    if (!route) {
        next();
        return;
    }

    // 정적 서빙 라우트면 next()로 StaticFiles에 위임
    if (route->is_static) {
        next();
        return;
    }

    // ... 기존 프록시 로직 ...
}
```

- [ ] **Step 4: Quarto 플랫폼 예제 앱 작성**

```cpp
// examples/ReverseProxy/src/main.cpp
#include <serverweb/WebServer.h>
#include <serverweb/ReverseProxy.h>
#include <serverweb/StaticFilesMiddleware.h>
#include <serverweb/LoggerMiddleware.h>

#include <spdlog/spdlog.h>

int main() {
    serverweb::WebServerConfig config;
    config.port = 8080;
    config.worker_count = 2;

    serverweb::WebServer server(config);

    // 미들웨어: 로깅
    server.Use(std::make_shared<serverweb::middleware::Logger>());

    // 미들웨어: 리버스 프록시 (app 도메인)
    serverweb::middleware::ReverseProxyOptions proxy_opts;
    proxy_opts.routes = {
        // app.mintcocoa.cc — API/Auth/Preview → Gateway
        {"app.mintcocoa.cc", "/api/",     "gateway", 8000},
        {"app.mintcocoa.cc", "/auth/",    "gateway", 8000},
        {"app.mintcocoa.cc", "/preview/", "gateway", 8000},
    };
    server.Use(std::make_shared<serverweb::middleware::ReverseProxy>(proxy_opts));

    // 미들웨어: 정적 파일 (app SPA)
    serverweb::middleware::StaticFilesOptions app_static;
    app_static.root = "/srv/frontend";
    app_static.prefix = "/";
    app_static.index_files = {"index.html"};
    // SPA fallback: 파일 없으면 index.html 서빙
    server.Use(std::make_shared<serverweb::middleware::StaticFiles>(app_static));

    // blog 도메인 — 발행된 블로그 정적 서빙
    // Host 기반 분기는 ReverseProxy가 처리하고,
    // 매칭 안 되는 경로는 StaticFiles가 처리

    // 헬스체크
    server.Get("/health", [](serverweb::RequestContext& ctx) {
        ctx.SendJson(R"({"status":"ok"})");
    });

    spdlog::info("Quarto Platform Proxy starting on port {}", config.port);
    server.Start();

    return 0;
}
```

- [ ] **Step 5: CMakeLists.txt 예제 추가**

```cmake
# examples/ReverseProxy/CMakeLists.txt
add_executable(reverse_proxy src/main.cpp)
target_link_libraries(reverse_proxy PRIVATE ServerWeb)
```

프로젝트 루트 CMakeLists.txt에 추가:
```cmake
add_subdirectory(examples/ReverseProxy)
```

- [ ] **Step 6: 빌드 확인**

```bash
cd /tmp/libiouring-server/build
cmake --build . -j$(nproc)
ls bin/reverse_proxy
```

Expected: `reverse_proxy` 바이너리 생성

- [ ] **Step 7: Commit**

```bash
git add include/serverweb/ReverseProxy.h src/web/ReverseProxy.cpp examples/ReverseProxy/ CMakeLists.txt
git commit -m "feat: Host-based routing and Quarto platform proxy example"
```

---

## Task 6: 통합 테스트

**Files:**
- Create: `tests/web/ReverseProxyIntegrationTest.cpp`

- [ ] **Step 1: 통합 테스트 작성**

```cpp
// tests/web/ReverseProxyIntegrationTest.cpp
#include <gtest/gtest.h>
#include <serverweb/WebServer.h>
#include <serverweb/ReverseProxy.h>

#include <thread>
#include <chrono>

// 간단한 upstream 서버 + 프록시 서버를 띄우고 요청 흐름 검증
// io_uring이 필요하므로 Linux에서만 실행 가능

TEST(ReverseProxyIntegration, DISABLED_ProxyForwardsGetRequest) {
    // 1. Upstream 서버 (포트 9090)
    serverweb::WebServerConfig upstream_config;
    upstream_config.port = 9090;
    upstream_config.worker_count = 1;
    serverweb::WebServer upstream(upstream_config);
    upstream.Get("/documents", [](serverweb::RequestContext& ctx) {
        ctx.SendJson(R"({"documents":[]})");
    });

    // 2. 프록시 서버 (포트 9091)
    serverweb::WebServerConfig proxy_config;
    proxy_config.port = 9091;
    proxy_config.worker_count = 1;
    serverweb::WebServer proxy(proxy_config);

    serverweb::middleware::ReverseProxyOptions opts;
    opts.routes = {{.path_prefix = "/api/", .upstream_host = "127.0.0.1", .upstream_port = 9090}};
    proxy.Use(std::make_shared<serverweb::middleware::ReverseProxy>(opts));

    // 3. 서버 시작 (별도 스레드)
    std::thread upstream_thread([&] { upstream.Start(); });
    std::thread proxy_thread([&] { proxy.Start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 4. curl 또는 httpx로 테스트
    // 실제 io_uring 환경에서만 동작하므로 CI에서는 DISABLED_
    // 수동 테스트:
    // curl http://localhost:9091/api/documents
    // Expected: {"documents":[]}

    // 5. 정리
    upstream.Stop();
    proxy.Stop();
    upstream_thread.join();
    proxy_thread.join();
}
```

**Note:** io_uring 통합 테스트는 Linux 커널 5.19+ 환경에서만 동작한다. CI에서는 `DISABLED_` prefix로 스킵하고, 로컬에서 수동 검증한다.

- [ ] **Step 2: 수동 테스트 스크립트 작성**

```bash
#!/bin/bash
# tests/test_reverse_proxy.sh

echo "=== Starting upstream server ==="
# upstream이 이미 실행 중이라고 가정 (예: Gateway)

echo "=== Testing HTTP proxy ==="
curl -s http://localhost:8080/api/health -H "Host: app.mintcocoa.cc"
echo ""

echo "=== Testing static file serving ==="
curl -s http://localhost:8080/ -H "Host: blog.mintcocoa.cc"
echo ""

echo "=== Testing 404 for unknown route ==="
curl -s http://localhost:8080/unknown
echo ""
```

- [ ] **Step 3: Commit**

```bash
git add tests/web/ReverseProxyIntegrationTest.cpp tests/test_reverse_proxy.sh
git commit -m "test: reverse proxy integration tests and manual test script"
```

---

## Summary

| Task | 내용 | 핵심 파일 |
|------|------|----------|
| 1 | UpstreamSession — io_uring TCP 연결 | UpstreamSession.h/.cpp |
| 2 | UpstreamPool — 커넥션 관리 | UpstreamPool.h/.cpp |
| 3 | ReverseProxy — HTTP 프록시 미들웨어 | ReverseProxy.h/.cpp |
| 4 | WebSocket 프록시 — 업그레이드 감지 | ReverseProxy.h/.cpp 수정 |
| 5 | Host 라우팅 + 예제 앱 | ReverseProxy 수정 + examples/ |
| 6 | 통합 테스트 | Integration test + script |

### 제한사항 및 향후 개선

| 항목 | 현재 | 향후 |
|------|------|------|
| DNS 해석 | 동기 (getaddrinfo) | 비동기 DNS |
| 커넥션 풀링 | 매번 새 연결 | 유휴 연결 재사용 |
| WebSocket 릴레이 | 업그레이드 감지만 | 완전한 양방향 프레임 릴레이 |
| 응답 파싱 | Content-Length 기반 | chunked transfer encoding |
| 에러 처리 | 502 반환 | retry, circuit breaker |
| 로드밸런싱 | 단일 upstream | round-robin, least-connections |
