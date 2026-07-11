#include "common.h"

unsigned long long get_factorial(unsigned int n) {
    if (n == 0) return 1;
    unsigned long long result = 1;
    for (unsigned int i = 1; i <= n; ++i) {
        result *= i;
    }
    return result;
}
