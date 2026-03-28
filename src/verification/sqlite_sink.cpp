// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/verification/sqlite_sink.hpp>
#include <sqlite3.h>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <vector>
#include <atomic>
#include <iostream>

namespace logos {

namespace {

struct AssertRecord {
    uint64_t timestamp_ns;
    uint64_t thread_id;
    uint64_t fiber_id;
    std::string req_id;
    std::string condition;
    std::string message;
    std::string source_file;
    uint32_t source_line;
    std::string call_chain_json;
};

struct TraceRecord {
    uint64_t timestamp_ns;
    uint64_t thread_id;
    uint64_t fiber_id;
    std::string tag;
    std::string source_file;
    uint32_t source_line;
    std::string data_json;
};

// Very basic global state for MVP
sqlite3* g_db = nullptr;
std::mutex g_mutex;
std::condition_variable g_cv;
std::vector<AssertRecord> g_asserts;
std::vector<TraceRecord> g_traces;
std::atomic<bool> g_running{false};
std::thread g_writer_thread;

void writer_loop() {
    while (true) {
        std::vector<AssertRecord> local_asserts;
        std::vector<TraceRecord> local_traces;
        
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_cv.wait(lock, []{
                return !g_asserts.empty() || !g_traces.empty() || !g_running.load(std::memory_order_relaxed);
            });
            
            local_asserts.swap(g_asserts);
            local_traces.swap(g_traces);
        }
        
        if (local_asserts.empty() && local_traces.empty() && !g_running.load(std::memory_order_relaxed)) {
            break;
        }
        
        if (!g_db) continue;
        
        char* err_msg = nullptr;
        sqlite3_exec(g_db, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg);
        
        // Write asserts
        if (!local_asserts.empty()) {
            sqlite3_stmt* stmt = nullptr;
            const char* sql = "INSERT INTO assertions (timestamp_ns, thread_id, fiber_id, requirement_id, condition, message, source_file, source_line, call_chain) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
            if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                for (const auto& a : local_asserts) {
                    sqlite3_bind_int64(stmt, 1, a.timestamp_ns);
                    sqlite3_bind_int64(stmt, 2, a.thread_id);
                    sqlite3_bind_int64(stmt, 3, a.fiber_id);
                    sqlite3_bind_text(stmt, 4, a.req_id.c_str(), -1, SQLITE_STATIC);
                    sqlite3_bind_text(stmt, 5, a.condition.c_str(), -1, SQLITE_STATIC);
                    sqlite3_bind_text(stmt, 6, a.message.c_str(), -1, SQLITE_STATIC);
                    sqlite3_bind_text(stmt, 7, a.source_file.c_str(), -1, SQLITE_STATIC);
                    sqlite3_bind_int(stmt, 8, a.source_line);
                    sqlite3_bind_text(stmt, 9, a.call_chain_json.c_str(), -1, SQLITE_STATIC);
                    sqlite3_step(stmt);
                    sqlite3_reset(stmt);
                }
                sqlite3_finalize(stmt);
            }
        }
        
        // Write traces
        if (!local_traces.empty()) {
            sqlite3_stmt* stmt = nullptr;
            const char* sql = "INSERT INTO traces (timestamp_ns, thread_id, fiber_id, tag, source_file, source_line, data) VALUES (?, ?, ?, ?, ?, ?, ?);";
            if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                for (const auto& t : local_traces) {
                    sqlite3_bind_int64(stmt, 1, t.timestamp_ns);
                    sqlite3_bind_int64(stmt, 2, t.thread_id);
                    sqlite3_bind_int64(stmt, 3, t.fiber_id);
                    sqlite3_bind_text(stmt, 4, t.tag.c_str(), -1, SQLITE_STATIC);
                    sqlite3_bind_text(stmt, 5, t.source_file.c_str(), -1, SQLITE_STATIC);
                    sqlite3_bind_int(stmt, 6, t.source_line);
                    sqlite3_bind_text(stmt, 7, t.data_json.c_str(), -1, SQLITE_STATIC);
                    sqlite3_step(stmt);
                    sqlite3_reset(stmt);
                }
                sqlite3_finalize(stmt);
            }
        }
        
        sqlite3_exec(g_db, "COMMIT;", nullptr, nullptr, &err_msg);
        if (err_msg) {
            std::cerr << "SQLite error: " << err_msg << std::endl;
            sqlite3_free(err_msg);
        }
    }
}

} // namespace

void init_sqlite_sink(const TraceDatabaseConfig& config) {
    if (g_db) return;
    
    if (sqlite3_open(config.path.c_str(), &g_db) != SQLITE_OK) {
        std::cerr << "Failed to open SQLite database: " << config.path << std::endl;
        return;
    }
    
    // Create schema
    const char* schema = R"(
        PRAGMA journal_mode = WAL;
        PRAGMA synchronous = NORMAL;
        
        CREATE TABLE IF NOT EXISTS assertions (
            id INTEGER PRIMARY KEY,
            timestamp_ns INTEGER NOT NULL,
            thread_id INTEGER,
            fiber_id INTEGER,
            requirement_id TEXT NOT NULL,
            condition TEXT,
            message TEXT,
            source_file TEXT,
            source_line INTEGER,
            call_chain TEXT
        );

        CREATE TABLE IF NOT EXISTS traces (
            id INTEGER PRIMARY KEY,
            timestamp_ns INTEGER NOT NULL,
            thread_id INTEGER,
            fiber_id INTEGER,
            tag TEXT NOT NULL,
            source_file TEXT,
            source_line INTEGER,
            data TEXT NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_traces_tag ON traces(tag);
        CREATE INDEX IF NOT EXISTS idx_assertions_req ON assertions(requirement_id);
    )";
    
    char* err_msg = nullptr;
    if (sqlite3_exec(g_db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "Failed to initialize schema: " << err_msg << std::endl;
        sqlite3_free(err_msg);
    }
    
    g_running.store(true, std::memory_order_release);
    g_writer_thread = std::thread(writer_loop);
}

void shutdown_sqlite_sink() {
    g_running.store(false, std::memory_order_release);
    g_cv.notify_one();
    if (g_writer_thread.joinable()) {
        g_writer_thread.join();
    }
    if (g_db) {
        sqlite3_close(g_db);
        g_db = nullptr;
    }
}

void record_assertion(
    uint64_t timestamp_ns,
    uint64_t thread_id,
    uint64_t fiber_id,
    std::string_view req_id,
    std::string_view condition,
    std::string_view message,
    const std::source_location& loc,
    std::string_view call_chain_json)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_asserts.push_back({
        timestamp_ns, thread_id, fiber_id, 
        std::string(req_id), std::string(condition), std::string(message),
        std::string(loc.file_name()), loc.line(), std::string(call_chain_json)
    });
    g_cv.notify_one();
}

void record_trace(
    uint64_t timestamp_ns,
    uint64_t thread_id,
    uint64_t fiber_id,
    std::string_view tag,
    const std::source_location& loc,
    std::string_view data_json)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_traces.push_back({
        timestamp_ns, thread_id, fiber_id, 
        std::string(tag), std::string(loc.file_name()), loc.line(), std::string(data_json)
    });
    g_cv.notify_one();
}

} // namespace logos
