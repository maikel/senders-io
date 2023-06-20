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

TODO

# Documentation

## Sequence Senders

## `async_resource`

## `byte_stream` / `seekable_byte_stream`

## `file_handle`

## `io_scheduler`

## `socket_handle`

## `net_scheduler`