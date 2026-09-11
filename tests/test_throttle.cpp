#include "../src/throttle.h"
#include <cstdio>
#include <thread>
#include <vector>
#include <algorithm>

static int g_fail = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL line %d: %s\n", __LINE__, #cond);               \
            g_fail++;                                                   \
        }                                                               \
    } while (0)

int main() {
    {
        SteamThrottle thr(50, 5000);
        CHECK(thr.CurrentInterval() == 50);
        const int kThreads = 8, kSlots = 12;
        std::vector<long long> stamps;
        std::mutex m;
        std::vector<std::thread> ts;
        for (int t = 0; t < kThreads; t++) {
            ts.emplace_back([&]() {
                for (int i = 0; i < kSlots; i++) {
                    thr.WaitTurn();
                    long long now =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
                    std::lock_guard<std::mutex> l(m);
                    stamps.push_back(now);
                }
            });
        }
        for (auto& t : ts) t.join();
        CHECK((int)stamps.size() == kThreads * kSlots);
        std::sort(stamps.begin(), stamps.end());
        int tight = 0;
        for (size_t i = 1; i < stamps.size(); i++)
            if (stamps[i] - stamps[i - 1] < 35) tight++;
        CHECK(tight <= 2);
    }
    {
        SteamThrottle thr(50, 5000);
        thr.WaitTurn();
        long long before =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        thr.ReportRateLimited(400);
        auto t0 = std::chrono::steady_clock::now();
        thr.WaitTurn();
        auto t1 = std::chrono::steady_clock::now();
        long long waited =
            std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        (void)before;
        CHECK(waited >= 340);
        CHECK(thr.CurrentInterval() > 50);
    }
    {
        SteamThrottle thr(250, 5000);
        thr.ReportRateLimited(1000000);
        CHECK(thr.CurrentInterval() == 600);
    }
    {
        SteamThrottle thr(250, 5000);
        for (int i = 0; i < 12; i++) thr.ReportSuccess();
        CHECK(thr.CurrentInterval() == 250);
    }

    if (g_fail == 0) printf("throttle: ALL OK\n");
    return g_fail == 0 ? 0 : 1;
}
