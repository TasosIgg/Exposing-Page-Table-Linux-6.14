// SPDX-License-Identifier: GPL-2.0-only
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define X 21

#define PERR_RET(cond, str) \
    do {                    \
        if (cond) {         \
            perror(str);    \
            return 1;       \
        }                   \
    } while (0)             \

static inline uint64_t clock_gettime_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int main(void) {
    uint64_t start_time, end_time;

    int ept_fd = open("/dev/ept", O_RDONLY);
    PERR_RET(ept_fd < 0, "open");

    unsigned long *ept = mmap(NULL, 1ULL << 39, PROT_READ, MAP_PRIVATE, ept_fd, 0);
    PERR_RET(ept == MAP_FAILED, "mmap");
    PERR_RET(close(ept_fd), "close");

    char *p = mmap(NULL, 1ULL << X, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    PERR_RET(p == MAP_FAILED, "mmap");
    madvise(p, 1ULL << X, MADV_NOHUGEPAGE);

    start_time = clock_gettime_ns();
    unsigned long pte = ept[(unsigned long)p >> 12];
    end_time = clock_gettime_ns();
    printf("1. Time: %lu (ns) -> <0x%lx>\n", end_time - start_time, pte);

    start_time = clock_gettime_ns();
    pte = ept[(unsigned long)p >> 12];
    end_time = clock_gettime_ns();
    printf("2. Time: %lu (ns) -> <0x%lx>\n", end_time - start_time, pte);

    p[0] = 'C';
    start_time = clock_gettime_ns();
    pte = ept[(unsigned long)p >> 12];
    end_time = clock_gettime_ns();
    printf("3. Time: %lu (ns) -> <0x%lx>\n", end_time - start_time, pte);

    start_time = clock_gettime_ns();
    pte = ept[(unsigned long)p >> 12];
    end_time = clock_gettime_ns();
    printf("4. Time: %lu (ns) -> <0x%lx>\n", end_time - start_time, pte);

    PERR_RET(munmap(p, 1ULL << X), "munmap");
    PERR_RET(munmap(ept, 1ULL << 39), "munmap");
    return 0;
}
