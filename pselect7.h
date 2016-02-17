#include <sys/select.h>

int pselect7(int nfds, fd_set *readfds, fd_set *writefds,
             fd_set *exceptfds, const struct timespec *timeout,
             const sigset_t *sigmask, int *signals_received);
