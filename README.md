# senders-io
An adaption of Senders/Receivers for async networking and I/O

This repository is still very experimental and in development. It's goal is to create abstractions for file and networking i/o using stdexec as a model for asynchronous operations.

The basic principles are:
- We use an `async_resource` concept to asynchronously create and asynchronously release a resource
  - such a resource is usable via some copyable, non-owning token such as a `file_handle` or `socket_handle`
  - examples are files, sockets, schedulers, memory ...
- We use *sequence-senders* for operations that could potentially complete multiple times
  - completion are either sequential or in parallel
  - examples are read and write operations, resolving hostnames or accepting new connections

# How to build this project

This repository builds ontop of a specific branch of stdexec, namely the `member-only-customization-points`.
(see https://github.com/NVIDIA/stdexec/pull/788)

Therefore you have to checkout the correct branch and install it such that CMake can find it.

To install stdexec in some `<prefix>` folder do something along the lines
```
$ git clone git@github.com:villevoutilainen/wg21_p2300_std_execution.git stdexec
$ git -w stdexec checkout member-only-customization-points
$ cmake -S stdexec -B stdexec-build -DCMAKE_INSTALL_PREFIX="<prefix>"
$ cmake --install stdexec-build
```
After having installed stdexec, you can specify the `<prefix>` folder to CMake via the `stdexec_DIR` variable as in

```
$ cmake -S senders-io -B senders-io-build -Dstdexec_DIR=<prefix>/lib/cmake/stdexec
```

This project has been tested with gcc-11 and clang-16.

# Documentation

## Sequence Senders

## `async_resource`

## `byte_stream` / `seekable_byte_stream`

## `file_handle`

## `io_scheduler`

## `socket_handle`

## `net_scheduler`