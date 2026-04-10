#include "types.h"
#include "net/io_worker.h"
#include "net/io_worker_pool.h"
#include "net/game_session.h"
#include "game/player_context.h"
#include "game/room_manager.h"
#include "db/memory_db_service.h"
#include "db/sqlite_db_service.h"
#include "db/pg_db_service.h"

#include <serverstorage/PgConnectionPool.h>
#include <serverstorage/StorageConfig.h>

#include <spdlog/spdlog.h>
#include <signal.h>
#include <atomic>
#include <cstdlib>
#include <string>
#include <thread>

static std::atomic<bool> g_running{true};
void SignalHandler(int) { g_running = false; }

int main() {
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    constexpr std::uint16_t kPort = 7777;
    constexpr std::uint16_t kWorkerCount = 2;

    spdlog::info("GameServer starting on port {}...", kPort);

    // 글로벌 서비스
    PlayerManager player_manager;
    IoWorkerPool worker_pool(kWorkerCount);
    RoomManager room_manager(worker_pool.GetGlobalQueue(), &worker_pool,
                             worker_pool.GetTimer());

    // DB backend — PostgreSQL by default, SQLite for offline development.
    //
    // GAMESERVER_DB=sqlite             → embedded SQLite (offline/dev mode)
    // GAMESERVER_DB_DSN=<libpq DSN>    → override PostgreSQL connection string
    //   (default: host=localhost dbname=gamedb user=game)
    std::unique_ptr<DbService> db;
    std::shared_ptr<serverstorage::PgConnectionPool> pg_pool;  // kept alive for db

    const char* db_env = std::getenv("GAMESERVER_DB");
    const bool use_sqlite = (db_env && std::string(db_env) == "sqlite");

    if (use_sqlite) {
        db = std::make_unique<SqliteDbService>("gameserver.db");
        spdlog::info("DB backend: SQLite (gameserver.db) [offline dev mode]");
    } else {
        serverstorage::StorageConfig pg_cfg;
        if (const char* dsn = std::getenv("GAMESERVER_DB_DSN")) {
            pg_cfg.connection_string = dsn;
        }
        pg_pool = std::make_shared<serverstorage::PgConnectionPool>(pg_cfg);
        bool pool_ok = false;
        pg_pool->Initialize([&pool_ok](bool success) { pool_ok = success; });
        if (!pool_ok) {
            spdlog::error("PostgreSQL pool initialization failed "
                          "(DSN: {}). Set GAMESERVER_DB=sqlite for offline mode.",
                          pg_cfg.connection_string);
            return 1;
        }
        db = std::make_unique<PgDbService>(pg_pool, pg_cfg);
        spdlog::info("DB backend: PostgreSQL ({})", pg_cfg.connection_string);
    }

    // 세션 ID 생성기
    static std::atomic<servercore::SessionId> g_next_sid{1};

    // 각 IoWorker 개별 시작
    servercore::Address addr{"0.0.0.0", kPort};

    for (std::uint16_t i = 0; i < kWorkerCount; ++i) {
        auto* worker = worker_pool.GetWorker(i);

        servercore::io::SessionFactory factory =
            [worker, &player_manager, &room_manager, db_ptr = db.get()]
            (int fd, servercore::ring::IoRing& ring,
             servercore::buffer::BufferPool& pool,
             servercore::ContextId shard_id)
            -> servercore::io::SessionRef
        {
            auto session = std::make_shared<GameSession>(fd, ring, pool, worker);
            auto sid = g_next_sid.fetch_add(1);
            session->SetSessionId(sid);
            session->SetServices(&player_manager, &room_manager, db_ptr);

            worker->AddSession(sid, session);
            return session;
        };

        worker->Start(addr, std::move(factory));
    }

    spdlog::info("GameServer ready - {} workers, port {}", kWorkerCount, kPort);

    // 메인 스레드: 시그널 대기 + 주기적 빈 Room 정리
    int cleanup_counter = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (++cleanup_counter >= 10) {
            cleanup_counter = 0;
            room_manager.CleanupEmptyRooms();
        }
    }

    spdlog::info("Shutting down...");
    worker_pool.StopAll();
    spdlog::info("GameServer stopped.");

    return 0;
}
