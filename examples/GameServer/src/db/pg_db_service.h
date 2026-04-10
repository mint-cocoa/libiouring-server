#pragma once
#include "db_service.h"

#include <serverstorage/PgConnectionPool.h>
#include <serverstorage/StorageConfig.h>

#include <memory>
#include <string>

// DbService implementation backed by serverstorage::PgConnectionPool.
//
// Each DbService method is translated into one or more PostgreSQL queries
// that are submitted to the worker-pool facade from 6장. Query callbacks
// are delivered back on the originating IoWorker thread via IoRing::Post,
// so handler code sees the same threading model it already expects from
// the SQLite path.
class PgDbService : public DbService {
public:
    // Takes shared ownership of an already-initialized connection pool.
    // `config` must match the DSN the pool was constructed with — it is
    // used to open a short-lived bootstrap connection for DDL execution.
    // Schema is created up-front from the calling thread.
    PgDbService(std::shared_ptr<serverstorage::PgConnectionPool> pool,
                serverstorage::StorageConfig config);
    ~PgDbService() override = default;

    void Login(const std::string& username, const std::string& password,
               std::function<void(bool, PlayerId)> cb) override;

    void Register(const std::string& username, const std::string& password,
                  std::function<void(bool, std::string)> cb) override;

    void GetCharList(PlayerId pid,
                     std::function<void(std::vector<CharInfo>)> cb) override;

    void CreateChar(PlayerId pid, const std::string& name,
                    std::function<void(bool, std::string, CharInfo)> cb) override;

    void LoadInventory(CharId cid,
                       std::function<void(std::vector<ItemData>)> cb) override;

    void LoadCurrency(CharId cid,
                      std::function<void(CurrencyData)> cb) override;

private:
    // Opens a throwaway libpq connection and executes the schema DDL
    // synchronously. Called from the constructor — thread-agnostic.
    void InitSchema();

    std::shared_ptr<serverstorage::PgConnectionPool> pool_;
    serverstorage::StorageConfig config_;
};
