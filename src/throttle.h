#pragma once
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>

class SteamThrottle {
public:
    SteamThrottle(int baseMs = 180, int maxMs = 5000)
        : m_nextAllowed(0), m_loginNext(0) {
        if (baseMs < 50) baseMs = 50;
        if (maxMs < baseMs) maxMs = baseMs;
        m_base.store(baseMs);
        m_max.store(maxMs);
        m_interval.store(baseMs);
        m_consecOk.store(0);
        m_loginBase.store(1100);
        m_loginMax.store(8000);
        m_loginInterval.store(1100);
m_loginOk.store(0);
    }

    void SetBase(int ms) {
        if (ms < 50) ms = 50;
        m_base.store(ms);
        int cur = m_interval.load();
        if (cur < ms) m_interval.store(ms);
        if (m_max.load() < ms) m_max.store(ms);
    }

    int CurrentInterval() const { return m_interval.load(); }
    int CurrentLoginInterval() const { return m_loginInterval.load(); }

    void WaitTurn() {
        long long slot;
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            long long now = NowMs();
            slot = now > m_nextAllowed ? now : m_nextAllowed;
            m_nextAllowed = slot + m_interval.load();
        }
        long long wait = slot - NowMs();
        if (wait > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(wait));
    }

    void WaitLoginTurn() {
        long long slot;
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            long long now = NowMs();
            slot = now > m_loginNext ? now : m_loginNext;
            m_loginNext = slot + m_loginInterval.load();
        }
        long long wait = slot - NowMs();
        if (wait > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(wait));
    }

    void ReportRateLimited(int waitMs) {
        if (waitMs < 0) waitMs = 0;
        if (waitMs > 60000) waitMs = 60000;
        std::lock_guard<std::mutex> lock(m_mtx);
        long long until = NowMs() + waitMs;
        if (until > m_nextAllowed) m_nextAllowed = until;
        if (until > m_loginNext) m_loginNext = until;
        int cur = m_interval.load() + 350;
        m_interval.store(cur > m_max.load() ? m_max.load() : cur);
        int li = m_loginInterval.load() + 700;
        m_loginInterval.store(li > m_loginMax.load() ? m_loginMax.load() : li);
        m_consecOk.store(0);
        m_loginOk.store(0);
    }

    void ReportSuccess() {
        int ok = m_consecOk.fetch_add(1) + 1;
        if (ok >= 4) {
            int base = m_base.load();
            int cur = m_interval.load();
            if (cur > base) m_interval.store(cur - 150 < base ? base : cur - 150);
            m_consecOk.store(2);
        }
        int lok = m_loginOk.fetch_add(1) + 1;
        if (lok >= 4) {
            int lbase = m_loginBase.load();
            int lcur = m_loginInterval.load();
            if (lcur > lbase)
                m_loginInterval.store(lcur - 200 < lbase ? lbase : lcur - 200);
            m_loginOk.store(2);
        }
    }

private:
    static long long NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    std::mutex m_mtx;
    std::atomic<int> m_base;
    std::atomic<int> m_max;
    std::atomic<int> m_interval;
    std::atomic<int> m_consecOk;
    std::atomic<int> m_loginBase;
    std::atomic<int> m_loginMax;
    std::atomic<int> m_loginInterval;
    std::atomic<int> m_loginOk;
    long long m_nextAllowed;
    long long m_loginNext;
};
