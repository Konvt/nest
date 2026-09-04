# nest::Hive – A learning implementation of P0447 (hive)

A personal, educational C++20 implementation of the proposed `std::hive` container, focusing on **standard‑conforming pointer reinterpretation, storage reuse, and type‑punning** – without relying on implementation‑defined behaviour.

## What this is
- A **learning exercise** – I wrote this to understand the complexities of the P0447 proposal.
- **Not production‑ready** – performance is not thoroughly benchmarked against the reference `plf::hive`.
- **Strictly C++20** – support fancy pointer, strong exception safety guarantee as required by the standard, and allocator-aware construction/destruction to stay within the standard's object‑lifetime rules.

## Requirements
- C++20 or higher compiler.
- Header‑only – just `#include "nest.hpp"`.

## Example
```cpp
#include "nest.hpp"

nest::Hive<int> hive;
auto it = hive.emplace( 42 );
hive.insert( 100 );
hive.erase( it ); // no other elements are moved

for ( int x : hive ) { } // order is unstable
```

## Performance note
In my benchmarks ($10^{7}$ insert/erase/re‑insert cycles), this implementation runs at about *75–85%* of the reference `plf::hive` under `-O3`.

## Feedback
Issues and PRs are welcome, but keep in mind this is a hobby project.

## License
MIT

## Reference
Test code is largely adapted from [cppref](https://en.cppreference.com/cpp/container/hive) examples.

The design follows the P0447 proposal and the original [`plf::hive`](https://github.com/mattreecebentley/plf_hive/blob/main/plf_hive.h) implementation.
