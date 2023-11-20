# P6 Multithreaded HTTP Proxy Server

Group 61 - Mason W. & Ben B.

***
# Table of Contents
- [P6 Multithreaded HTTP Proxy Server](#p6-multithreaded-http-proxy-server)
- [Table of Contents](#table-of-contents)
- [Overview](#overview)
  - [Locks and CVs](#locks-and-cvs)

***

# Overview

In `proxyserver.c`, we separated out the `listen_forever()` function into two separate functions: `listen_forever()` and `serve_forever()`, one to be run with each type of thread. In order to create the threads we added code to the `main()` function that iterated to the provided numbers of threads of each type and called `pthread_create()`. For listeners, we included an argument struct that allowed us to pass in several arguments to the `listen_forever()` function. **On revision, it is possible for us to forego the struct entirely, for the only piece of information that actually needs to be passed in is the port number, but that is a small detail**. After we created the listeners and workers, we used `pthread_join()` to allow the main thread to wait for each to complete. 

In `safequeue.c`, we implemented a priority queue. All of the locking around the queue and its operations we decided to implement in `proxyserver.c`, so the priority queue implementation is fairly general.

## Locks and CVs

We used two locks:
- `mutex` for maintaining concurrency between thread operations
- `qlock` for maintaining concurrency between queue operations
Our different types of threads normally lock and unlock both of these locks in the same space.

We used two condition variables:
- `fill` in order to wake up consumers when producers had placed an item in the queue
- `empty` in order to provide an additional level of security for producers trying to add something to the queue such that they would not exceed the queue size limit. This is somewhat extraneous, as the checks for full and empty surround the calls to `add_work` or `get_work`/`_nonblocking()`, but it can't hurt to have.