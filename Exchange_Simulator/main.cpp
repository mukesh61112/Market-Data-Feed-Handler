#include "ExchangeSimulator.h"

int main() {
    ExchangeSimulator ex(9000, 100);

    ex.set_tick_rate(10000); // can scale to higher
    ex.enable_fault_injection(false);

    ex.start();
    ex.run();

    return 0;
}
