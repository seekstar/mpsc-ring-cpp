# Multiple Producer Single Consumer (MPSC) ring channel

Lock-free unless the channel is empty.

Tests: <https://github.com/seekstar/tests-channel/tree/mpsc-ring>

## Credits

Using semaphore to control the entrance of senders is inspired by `tokio::sync::mpsc`.

The implementation of ring buffer references `kfifo` in Linux kernel.

## LICENSE

This project is dual licensed under the Apache License v2.0 and the MIT License.
