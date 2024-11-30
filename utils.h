#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

const size_t k_max_msg = 4096;

inline void die(const std::string &s) { std::cout << "Failure: " << s << "\n"; }

static int32_t read_full(int fd, char *buf, size_t n)
{
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) {
            std::cout << "Didn't read anything\n";
            return -1;  // error
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n)
{
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) {
            return -1;  // error
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}
