#include "monitoring.h"

int main(void) {
    if (sig_init() != 0) {
        return 1;
    }
    return admin_run_loop("transactions.log", "atm", 1, 0);
}