#include <iostream>
#include <thread>
#include <chrono>
#include "SymbolCache.h"

int main() {
    SymbolCache cache(100);

    // Writer thread
    std::thread writer([&]() {
        for (int i = 0; i < 100000; ++i) {
            cache.updateBid(1, 100.5 + i, 10 + i, i);
            cache.updateAsk(1, 101.5 + i, 20 + i, i);
            cache.updateTrade(1, 100.8 + i, 5 + i, i);
        }
    });

    // Reader thread
    std::thread reader([&]() {
        for (int i = 0; i < 10; ++i) {
            auto snap = cache.getSnapshot(1);

            std::cout << "Bid: " << snap.best_bid
                      << " Ask: " << snap.best_ask
                      << " Last: " << snap.last_traded_price
                      << " Updates: " << snap.update_count
                      << std::endl;

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    writer.join();
    reader.join();

    return 0;
}