# lock-free-concurrent-skip-list

Three implementations of a skip list in C++ and a benchmark comparing them under mixed read/write workloads.

## Implementations

| File | Description |
|---|---|
| `skip_list_sequential.h` | Baseline single-threaded skip list using `shared_ptr` |
| `skip_list_locked.h` | Coarse-grained locking: a single `std::mutex` over the entire sequential list |
| `skip_list_lockfree.h` | Lock-free using `std::atomic<uint64_t>` with the low bit as a deletion mark ([Harris 2001](https://timharris.uk/papers/2001-disc.pdf) / [Herlihy-Shavit 2006](https://www.cs.tau.ac.il/~shanir/nir-pubs-web/Papers/OPODIS2006-BA.pdf)) |
| `epoch.h` | Epoch-based reclamation: defers freeing a removed node until no thread could still hold a pointer into it |

The lock-free implementation uses the standard mark-then-unlink deletion pattern:
1. A node's `next` pointer's low bit doubles as a "logically deleted" mark, exploiting 8-byte pointer alignment
2. Deletions mark from the top level down, finishing at level 0 (the deciding level)
3. Any thread that encounters a marked node during `find` physically unlinks it via CAS (the helping pattern)
4. Physically unlinked nodes are handed to `EBR` for reclamation rather than freed immediately or leaked

## Build and run

```
make run
```

## Benchmark results

Workload: 70% find, 20% insert, 10% remove. Key range 0–999, list pre-populated with 500 keys, each run lasts 2 seconds.

| Threads | Sequential | Locked    | Lock-Free  |
|---------|-----------:|----------:|-----------:|
| 1       | 9.5M       | 10.1M     | 10.8M      |
| 2       | —          |  4.0M     | 18.9M      |
| 4       | —          |  1.5M     | 22.7M      |
| 8       | —          |  1.0M     | 29.5M      |
| 16      | —          |  1.3M     | 38.0M      |

The locked version degrades as threads contend on the single mutex. The lock-free version keeps scaling through 16 threads and is ~29× faster than locked at that point. These numbers include epoch-based reclamation's per-operation cost (a pin/unpin pair per `find`/`insert`/`remove`); an earlier version without reclamation reached ~58M ops/sec at 16 threads but leaked every removed node.

## References

- Herlihy, Lev, Luchangco, Shavit. *A Provably Correct Scalable Concurrent Skip List.* OPODIS 2006. https://www.cs.tau.ac.il/~shanir/nir-pubs-web/Papers/OPODIS2006-BA.pdf
