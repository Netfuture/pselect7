# `pselect7()` - A fairer alternative to the `pselect()` system call

When dealing with multiple network connections or timeouts, the `select()` Unix system call is still the workhorse for many applications. Its well-known and frequently used interface beats the learning curve on the more scalable `poll()`, `epoll()`, or `/dev/poll` interfaces, especially if only a few file descriptors have to be monitored. `select()`‘s younger sibling, `pselect()`, adds improved signal handling while retaining interface simplicity. However, when not being extra careful, applications changing to `pselect()` can ignore network messages for many minutes, as we had to learn the hard way on a medium-to-well loaded large-scale mail server.

## `select()` revisited

```C
int select(int nfds, fd_set *readfds, fd_set *write_fds,
           fd_set *exceptfds, struct timeval *timeout);
```
The purpose of `select()` is to

*    wait for activity on three sets of file descriptors (Reading possible? Writing possible? Special events waiting, e.g. urgent messages?), return `value > 0`;
*    optionally time out after a number of seconds and microseconds (when `timeout != NULL`), `return value == 0`; or
*    terminate on a signal (as most system calls which can sleep), `return value < 0 && errno == EINTR`.

In many server processes, signals such as `SIGHUP` to reload configuration, `SIGTERM` to shut down cleanly, or `SIGCHLD` to respawn worker processes require interaction with the main loop. In this case, it is often easier to perform the operation not in the signal handler itself but in the main loop, as this avoids problems with the asynchronous nature of signals.

For example, the signal handlers in the [Cyrus IMAP mail server](https://www.cyrusimap.org) consist of a single assignment to a “have seen a signal” variable, which is then checked in the main event loop. However, if a signal is delivered outside of the `select()` system call, `select()` is obviously not woken up and the signal is only handled when the next event on one of the file descriptors arrives or the system call times out.

**Problem:** Signal handling can be delayed arbitrarily, especially on lightly or unloaded servers.

The **solution** would be to have signals delivered only during the `select()` syscall. Blocking or masking all signals before entering the main loop and then only quickly unblocking around the `select()` does not work, as the signals will be delivered between the unmasking and the call to `select()`, so select() will remain clueless about the signal just delivered. Additional probing can be done, but race conditions will remain.

## `pselect()`: Toward a message-like handling of signals

This is what `pselect()` was designed to remedy: In a main loop with `pselect()`, signals are masked, while they are atomically unblocked when entering/exiting `pselect()`. So in effect, `pselect()` allows the programmer to treat signals as an additional kind of message.

```C
int pselect(int nfds, fd_set *readfds, fd_set *writefds,
            fd_set *exceptfds, const struct timespec *timeout,
            const sigset_t *sigmask);
```

The main differences to `select()` are:

*    Atomic un-/re-masking of signals according to sigmask akin to using pthread_sigmask().
*    timeout is now in nanoseconds (ns), no longer in microseconds (μs).
*    timeout is not updated to reflect (an estimate of) the unused waiting time.

However, this signals-as-a-message treatment is not as well integrated as it seems at first. On servers with a high rate of signals, and with a main event loop taking enough time to always have a signal pending when `pselect()` is called next, the presence of active file descriptors is never recognized or handled.

On our 32-core Cyrus IMAP mail server, on certain load patterns, the thousands of IMAP daemons kept the Cyrus master scheduling process busy enough with respawning of dying processes (notified through `SIGCHLD`) that it was unable to recognize when living processes became busy (notified through a socket). Thus, the IMAP server stopped accepting new connections even though the system was not heavily loaded. This happened especially on Monday and Thursday mornings, when the rise in IMAP connections was steepest. (Note that using brute-force bulk load tests does not easily reproduce the problem.)

**Problem:** File descriptor handling can be delayed arbitrarily, especially on heavily loaded servers.

The solution would be to have signals delivered with the same priority as file descriptor events. However, with the current overloading of return values, this is not possible.

## Introducing `pselect7()`

In the tradition of naming extended versions of system calls with the number of arguments it now has (see `wait3()` and `wait4()` for prominent representatives), I propose an extension of `pselect()`, which treats signals and file descriptors that have accumulated before the call to `pselect()` with the same priority. Signals or FDs becoming ready during the wait will be returned on a first-come-first-serve basis.

To achieve this, `pselect7()` will retry `pselect()` with a zero timeout, whenever `pselect()` had returned `EINTR`. The zero timeout will ensure that only file descriptors ready immediately will be returned. Whether signals have occurred will be reported to the caller in the `int` pointed to by the signals_received argument, which may be passed as `NULL`, if this information is not desired.

The easiest is to copy/paste the above into your existing code and instead of `pselect(…)` now call `pselect7(…, NULL)`, and suddenly your program will continue to work as expected under high signal load. Of course, you are free to use the signals_received argument to learn whether signals have occurred during `pselect7()`.

The following is a simple usage example:

```C
int retval, signals_received;
retval = pselect7(nfds, readfds, writefds, exeptfds, timeout, sigmask, &signals_received);
/* Both retval and signals_received can be > 0 at the same time */
if (retval > 0) handle_fds(readfs, writefds, exceptfds);
if (signals_received > 0) handle_signal_results();
/* The following two cannot occur if at least one fired above */
if (retval == 0 && signals_received == 0) handle_timeout();
if (retval < 0) perror("pselect7");
```

## Alternatives

Linux provides the (non-portable) `signalfd()` system call, which allows a process to read its pending signals from a file descriptor, which can also be passed to `select()`, `poll()`, `epoll()`, or `/dev/poll` APIs. This allows for a clean integration of the two message types, but is not an option for applications that should run on other (POSIX-compatible) operating systems as well.

## Acknowledgments

This information and code is based on knowledge gained in part through intense interactions, technical discussions, and experiments with and by Jacob Becker, [Jens Erat](http://www.jenserat.de), and Christian Mack over the past few months.

[My blog entry](https://netfuture.ch/2016/02/pselect-pitfalls/) contains more background and information.
