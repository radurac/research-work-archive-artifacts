#include <errno.h>
#include <stdint.h>
#include <unistd.h>

static inline int efd_read(int fd, uint64_t *out) {
    ssize_t n;

    do {
        n = read(fd, out, sizeof *out);
    } while (n < 0 && errno == EINTR);

    return n == (ssize_t)sizeof *out ? 0 : -1;
}
