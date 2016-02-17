#include "pselect.h"

int pselect7(int nfds, fd_set *readfds, fd_set *writefds,
             fd_set *exceptfds, const struct timespec *timeout,
             const sigset_t *sigmask, int *signals_received)
{
  static const struct timespec zeroto = {0, 0};
  if (signals_received != NULL) *signals_received = 0;
  while (1) {
    int retval = pselect(nfds, readfds, writefds, exceptfds,
                         timeout, sigmask);
    if (retval != -1 || errno != EINTR) return retval;
    /* If a signal has been received,
       retry with zero timeout to obtain the pending fdsets */
    if (signals_received != NULL) *signals_received = 1;
    timeout = &zeroto;
  }
}
