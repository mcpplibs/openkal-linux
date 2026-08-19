# openkal-linux

The reference implementation of [openkal](https://github.com/mcpplibs/openkal)
for Linux. It is a usable backend, and it is maintained as the worked example
that other implementations follow.

## Interfaces provided

| Module | Notes |
| --- | --- |
| `openkal.abort` | terminates through `_exit`, so that no exit handler runs |
| `openkal.stream` | descriptors 0, 1 and 2; writes are completed or reported |
| `openkal.memory` | built upon the C library allocator, as clause 7.3 requires |

Vectored writes are not provided. The absence is expressed by declaring nothing,
and `kal::has_write_vectored<kal::stream>` is correspondingly false.

## Use

```toml
[dependencies]
openkal = "0.1.0"

[target.'cfg(os = "linux")'.dependencies]
openkal-linux = "0.1.0"
```

## Points of interest for other implementations

The implementation is short, and the following aspects of it are the ones the
specification expects to be reproduced.

**The interface module adds nothing.** `src/stream.cppm` consists of an export
and a re-export. An implementation may add only the overloads the specification
lists as optional capabilities of the interface, and this one adds none.

**Interruption is retried, not reported.** A caller cannot distinguish an
interrupted call from a genuine failure without knowledge of the platform. An
implementation that reports it produces short transfers on any system that
delivers signals, and such a defect is unlikely to appear during testing.

**Errors are translated through a table.** The platform's error values are
mapped onto the closed set the specification defines. Translation preserves the
naturalness requirement of clause 7.1; reconstructing a foreign namespace would
not.

**Allocation is built upon the C library allocator.** Clause 7.3 requires this.
The C library's formatted output is coupled to its own allocator, so an
implementation that introduced a second one would place two claimants on one
region of memory.

**Positioning is absent, and its absence confirms the decomposition.** On Linux,
whether a stream can be repositioned is a property of the individual descriptor:
the same implementation succeeds for a regular file and fails for a pipe. Had
`openkal.stream` offered positioning, this implementation could neither claim it
honestly nor withhold it usefully. Clause 6.3 places positioning in
`openkal.fs`, whose resource is a descriptor, and writing this implementation is
what confirms that clause independently of the reasoning that produced it.

## Conformance

`mcpp test` runs the suite. It verifies both halves of the claim: that the
operations provided behave as specified, and that the operation not provided is
absent, which is asserted at compile time.

## License

Apache-2.0.
