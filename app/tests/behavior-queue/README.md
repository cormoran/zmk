# Behavior queue unit tests

Run from the ZMK worktree in its West workspace:

```sh
west twister -T app/tests/behavior-queue -p native_sim/native/64 \
  --outdir build/behavior-queue-tests
```

The suite also supports `native_sim` when the host has a 32-bit C toolchain.
Both the normal and split variants use the production queue implementation,
real Zephyr message queues and spinlocks, a deterministic clock, and a manually
advanced worker. Tests cover admission and execution deadlines, accumulated
waits, elapsed active waits, callback delays, recursive enqueueing, queue-full
accounting, tick rounding, and 32-bit timestamp wraparound.
