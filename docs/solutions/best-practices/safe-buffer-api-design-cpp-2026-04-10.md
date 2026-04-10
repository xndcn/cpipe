---
title: Safe Buffer Management API Design in C++
date: 2026-04-10
category: best-practices
module: cpipe::platform
problem_type: best_practice
component: tooling
severity: high
applies_when:
  - Designing a buffer or memory pool in C/C++
  - Exposing a C ABI with bitmask enums
  - Writing factory functions for types with validation invariants
  - Processing untrusted input that determines allocation sizes
  - Targeting both 32-bit and 64-bit platforms
related_components:
  - "include/cpipe/types.h"
  - "include/cpipe/buffer.h"
  - "src/platform/common/buffer_pool.cpp"
  - "src/common/types.hpp"
tags:
  - buffer-pool
  - memory-safety
  - integer-overflow
  - bitmask-enum
  - c-abi
  - api-design
  - c-plus-plus
---

# Safe Buffer Management API Design in C++

## Context

During M1 Phase 1 of the cpipe computational photography pipeline, three rounds of code review uncovered 8 safety and correctness issues in the buffer management subsystem (`BufferDescriptor`, `Buffer`, `BufferPool`) and public C types. None were caught by the compiler or test suite at the time of introduction. (auto memory [claude]: M1 Phase 1 was implemented 2026-04-10 as foundational infrastructure for all subsequent milestones.)

These issues recur in any C++ codebase that manages raw memory buffers with heterogeneous device targets, exposes a C ABI for plugin interoperability, or allows descriptors to flow through validation-free construction paths. This document distills the 7 safety patterns that emerged from the fixes.

## Guidance

### 1. Cache keys must capture full identity, not just size

When pooling allocated buffers, the lookup key must include every property that makes two buffers non-interchangeable.

```cpp
// BEFORE: keyed by byte size — CPU buffer reused for GPU request
std::unordered_map<uint64_t, std::vector<void*>> free_list_;

// AFTER: keyed by full descriptor (format + dimensions + device)
struct DescriptorHash {
    size_t operator()(const BufferDescriptor& d) const noexcept {
        size_t h = std::hash<uint32_t>{}(d.width());
        h ^= std::hash<uint32_t>{}(d.height()) << 1;
        h ^= std::hash<int>{}(static_cast<int>(d.format())) << 2;
        h ^= std::hash<int>{}(static_cast<int>(d.device())) << 3;
        return h;
    }
};
std::unordered_map<BufferDescriptor, std::vector<void*>, DescriptorHash> free_list_;
```

### 2. Validate all enum inputs at the API boundary

Any C enum value crossing an ABI boundary can hold out-of-range values. Check before using in arithmetic.

```cpp
uint32_t bpp = bytes_per_pixel(fmt);
if (bpp == 0) return unexpected(Error::INVALID_PARAM); // unknown format
```

Without this check, `bytes_per_pixel()` returning 0 produces stride=0, size=0, and `aligned_alloc(64, 0)` — undefined behavior.

### 3. Guard every multiplication that feeds an allocation size

Three separate overflow checks are needed in the descriptor factory:

```cpp
// Guard 1: width * bpp must not overflow uint32_t
if (width > UINT32_MAX / bpp)
    return error("row bytes overflow");

uint32_t row_bytes = width * bpp;

// Guard 2: alignment rounding (row_bytes + 63) must not overflow uint32_t
if (row_bytes > UINT32_MAX - 63)
    return error("stride overflow");

uint32_t stride = (row_bytes + 63) & ~63u;

// Guard 3: stride * height must not overflow uint64_t
if (static_cast<uint64_t>(stride) > UINT64_MAX / height)
    return error("total size overflow");
```

### 4. Guard size_t truncation on 32-bit targets

When a validated `uint64_t` is cast to `size_t` for an allocator call, check for truncation:

```cpp
uint64_t size = desc.size();
if (size > SIZE_MAX)
    return unexpected(Error::OUT_OF_MEMORY);
// Now safe to cast
void* ptr = std::aligned_alloc(64, static_cast<size_t>(size));
```

On 64-bit targets the check is optimized away (SIZE_MAX == UINT64_MAX).

### 5. Bitmask enums must use power-of-two values

```cpp
// BEFORE: CPU=0 is invisible in bitwise OR
enum cpipe_device_t { CPU = 0, GPU = 1, NPU = 2 };
// CPU | GPU = 0 | 1 = 1 — indistinguishable from GPU-only

// AFTER: each device is a distinct bit
enum cpipe_device_t { CPU = 1, GPU = 2, NPU = 4 };
// CPU | GPU = 1 | 2 = 3 — each testable independently
```

### 6. Eliminate default constructors that bypass validation

If a type has invariants enforced by a factory, make the default constructor private:

```cpp
class BufferDescriptor {
public:
    static expected<BufferDescriptor, Error> create(uint32_t w, uint32_t h, ...);
private:
    BufferDescriptor() = default; // only create() can construct
};

// Compile-time enforcement:
static_assert(!std::is_default_constructible_v<BufferDescriptor>);
```

Same for `Buffer` — only `BufferPool::allocate()` can create valid handles:

```cpp
class Buffer {
private:
    explicit Buffer(std::shared_ptr<BufferData> d) : data_(std::move(d)) {}
    friend class BufferPool;
};
```

### 7. Use std::call_once for lazy singleton initialization

```cpp
namespace {
    std::once_flag s_init_flag;
    std::shared_ptr<spdlog::logger> s_logger;

    void ensure_logger(spdlog::level::level_enum lvl) {
        std::call_once(s_init_flag, [lvl] {
            s_logger = spdlog::stdout_color_mt("cpipe");
            s_logger->set_level(lvl);
        });
    }
}
```

Without `call_once`, two threads racing into lazy init can both call `register_logger()` — the second throws for the duplicate name.

## Why This Matters

Buffer management sits at the foundation of an image processing pipeline. Every node reads from and writes to buffers. A single flaw produces:

- **Silent data corruption**: CPU buffer reused for GPU request → stale data the GPU never wrote
- **Heap buffer overruns**: Integer overflow in size calculations → under-sized allocation → writes exceed boundary
- **Undefined behavior**: Zero-byte allocations, null dereferences from default-constructed handles
- **Security vulnerabilities**: Untrusted image metadata (width, height, format) flows directly into allocation arithmetic

All 8 issues were caught in code review. In production they would manifest as intermittent crashes, wrong-colored pixels, or exploitable memory corruption — all difficult to diagnose because the root cause (a wrong cache key, a missing overflow check) is far from the symptom.

## When to Apply

- Designing any buffer/memory pool in C or C++
- Exposing a C ABI for plugin systems (enum encoding, error codes)
- Writing factory functions for types with invariants (private default ctor pattern)
- Initializing singletons in multithreaded code (`std::call_once`)
- Processing untrusted input that determines allocation sizes (overflow guard chain)
- Targeting both 32-bit and 64-bit architectures (`size_t` truncation guard)
- Defining bitmask enums in any language (power-of-two values)

## Examples

### Cache key completeness

| Before | After |
|--------|-------|
| `unordered_map<uint64_t, ...>` (size only) | `unordered_map<BufferDescriptor, ...>` (all fields) |
| CPU buffer served for GPU request | Impossible — descriptors must match |

### Construction safety

```cpp
// BEFORE: two ways to get a descriptor
auto good = BufferDescriptor::create(1920, 1080, RGBA8, CPU); // validated
BufferDescriptor bad{};  // compiles, bypasses validation
pool.allocate(bad);       // aligned_alloc(64, 0) — UB

// AFTER: one way
auto good = BufferDescriptor::create(1920, 1080, RGBA8, CPU); // validated
// BufferDescriptor bad{};  // COMPILE ERROR
```

### Bitmask encoding

```cpp
// BEFORE: CPU=0, GPU=1
uint32_t mask = CPU | GPU; // = 1 (CPU lost)

// AFTER: CPU=1, GPU=2
uint32_t mask = CPU | GPU; // = 3 (both preserved)
```

## Related

- `docs/architecture.md` — BufferPool design intent and C ABI specification
- `docs/m1-plan.md` — M1 implementation plan (note: device enum values in plan are outdated relative to implementation)
- `docs/solutions/best-practices/cpp20-cmake-vcpkg-project-scaffolding-2026-04-10.md` — M0 project setup
