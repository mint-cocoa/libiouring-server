# WebServer Example

ServerCore의 HTTP 프레임워크(`ServerWeb` + `ServerStorage`)를 사용한 REST API 서버 예제.
io_uring 기반 비동기 I/O, radix-tree 라우터, 미들웨어 파이프라인, PostgreSQL 비동기 쿼리를 시연한다.

## Quick Start

```bash
# 빌드 (프로젝트 루트에서)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target WebServer

# 실행 (In-Memory 모드)
cd examples/WebServer
../../bin/WebServer
# → http://localhost:8080

# PostgreSQL 모드
WEBSERVER_DB_DSN="host=localhost dbname=webdemo user=app" ../../bin/WebServer
```

## Architecture

```
Handler → Service → DB Interface ← MemoryDb / PgDb
```

요청 흐름: HTTP Request → Middleware (Logger, CORS) → Router → Handler → Service → DB → Response

### Directory Structure

```
src/
├── main.cpp                    # 서버 설정, 미들웨어, 계층 조립
├── model/
│   └── item.h                  # Item 구조체 + JSON 직렬화
├── db/
│   ├── db_service.h            # IDbService 추상 인터페이스
│   ├── memory_db.h             # In-Memory 구현 (기본)
│   ├── pg_db.h / pg_db.cpp     # PostgreSQL 구현 + 스키마 부트스트랩
├── service/
│   └── item_service.h          # 비즈니스 로직 계층
└── handler/
    ├── item_handler.h/cpp      # /api/items CRUD 엔드포인트
    └── health_handler.h/cpp    # /api/health 상태 확인

public/
├── index.html                  # 대시보드 SPA
├── style.css                   # 시스템 모니터링 UI 테마
└── app.js                      # CRUD + 실시간 텔레메트리
```

### Layered Design

| Layer | Role | Example |
|-------|------|---------|
| **handler** | HTTP ↔ JSON 변환, 입력 검증 | `ItemHandler::Register()` |
| **service** | 비즈니스 로직 | `ItemService::CreateItem()` |
| **db** | 데이터 접근 (인터페이스) | `IDbService::InsertItem()` |
| **model** | 순수 데이터 구조체 | `Item`, `ItemToJson()` |

새 도메인 추가 시 각 계층에 파일을 하나씩 추가하면 된다.

## API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/api/items` | 전체 아이템 목록 |
| `GET` | `/api/items/:id` | 아이템 단건 조회 |
| `POST` | `/api/items` | 아이템 생성 (`{"name": "...", "price": 0}`) |
| `DELETE` | `/api/items/:id` | 아이템 삭제 |
| `GET` | `/api/health` | 서버 상태 + 모드 확인 |
| `GET` | `/` | 대시보드 SPA (HTML inline) |

## ServerCore Framework Features Used

- **WebServer** — multi-worker HTTP 서버 (SO_REUSEPORT, ring-per-thread)
- **Router** — radix-tree 기반 경로 매칭 (`:param`, `*wildcard`)
- **RouteGroup** — 접두사 기반 라우트 그룹 (`/api`)
- **Middleware** — Logger (요청 로깅), CORS (교차 출처 허용)
- **RequestContext** — `Defer()` + `SendJson()` 비동기 응답 패턴
- **PgConnectionPool** — 워커풀 기반 비동기 DB 쿼리 (콜백은 IoRing::Post로 원래 스레드에 전달)

## Modes

| Mode | 조건 | 저장소 |
|------|------|--------|
| **In-Memory** | `WEBSERVER_DB_DSN` 미설정 (기본) | `MemoryDb` — 프로세스 메모리, 재시작 시 초기화 |
| **PostgreSQL** | `WEBSERVER_DB_DSN` 설정 | `PgDb` — 비동기 쿼리, 영구 저장 |

## Demo UI

브라우저에서 `http://localhost:8080` 접속 시 대시보드 SPA가 로드된다.
CSS/JS는 서버 시작 시 HTML에 인라인되어 단일 요청으로 전체 UI가 전달된다.

- **Item Registry** — CRUD 폼 + 테이블 (삽입/삭제 애니메이션)
- **Telemetry** — 모드, 업타임, 아이템 수, 요청 레이턴시 실시간 표시
- **Request Log** — 모든 API 호출을 실시간 스트리밍
- **Architecture** — 프레임워크 스펙 요약
