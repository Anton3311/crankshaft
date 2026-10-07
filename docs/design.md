# About

This document describes the design considerations and approaches for solving common problems seen through throughout the compiler.

## Assertions

Assertions are used everywhere throughout the codebase. They help verify, that the program is in a state it is expected to be.

They are used to verify function inputs, test pre/post conditions and implications:

```c
// a -> b
if (a) {
    assert(b);
}
```

There are multiple variants of them:
1. `assert`
2. `assert_msg` - just like `assert`, but with an extra formattable message
3. `panic`
4. `unreachable`
5. `unreachable_msg`

At the point of writing this, the whole codebase contains around 670 assertions (not considering `panic` and `unreachable`) excluding the ones used in tests.

Together with ones in tests, the number reaches almost 1800 assertions.

## Arenas

`Cranshaft` heavily relies on memory arenas. They are used as a the primary way to allocate memory.

Usually there two arenas available at any point: `allocator` - the main arena for persistent allocations and `temp_allocator` - a temporary arena, used for temporary allocations, that shouldn't escape the scope of the function or a particular task. This achieved by `arena_begin_temp` and `arena_end_temp`.

The implementation for the arena lives in `core/src`. It relies on reserving a continuous range of virtual address space, and commiting pages on demand as allocations are being done.

Memory arenas provide multiple advantages, mainly: low allocation overhead and grouped lifetimes.

However, there one more point worth noting: the implementation used `crankshaft` guarantes stable pointers. The virtual address space range is reserved once and never gets moved around, or reallocated.

---

There are plenty of examples for grouping lifetimes with arenas.

One of them, is the parses. It uses a single arena, to allocate the whole AST. AST Nodes rely on simple raw pointers to reference each other, thanks to stable pointers. Furthermore, you won't find any of the deallocation code for the AST. Since it is completely in a single arena, deallocating it is as simple as resetting the arena pointer back to the start, or simply calling `arena_release` to return all of the memory pages back to the OS.

---

A common pattern with arenas is using it as a backing allocator for a dynamic array. Since an arena uses a large continuous range of virtual memory under the hood, growing the dynamic array can be done without moving any of the elements to new spot in memory. Simply allocating one more item in the arena, will put it at the end of the dynamic array, as long as it is not intertwined with other allocations.

```c
// Arena implementation allows allocating zero-sized blocks of
// memory, exactly to allow this pattern.
int* items = arena_alloc_array(&arena, int, 0);
size_t count = 0;

arena_alloc(&arena, int);
items[count] = 0;
count += 1;

arena_alloc(&arena, int);
items[count] = 1;
count += 1;
```

## Length based strings

`Crankshaft` avoids using null-terminated C strings as much as possible. The whole codebase relies on length based strings, which are defined and implemented in `core/src`. There you will also find functions for working with them and for converting them to and from regular C strings (for example, for interacting with C standard library or OS APIs).

Having to convert to and from regular C strings, does incur some overhead. However, the gains from length-based strings far outweigh them. Since now, such simple operations like taking a sub-string are done with zero allocations.

Furthermore, it allows implementing the whole tokenizer with 0 allocations, by simply referencing parts of the original source code string in each token. A single source file can easily have thousands of tokens and doing a memory allocation (even with an arena), would incur a significant performance penalty.

## Static Arrays

C being a simple language doesn't provide any way to implement a generic dynamic array. That leaves us with either implementing one for each type, or using code generation.

However, there is a way to avoid the need for a dynamic array altogether, by allocating a static array of maximum capacity needed for the task. This, however, does require knowing the exact capacity or at least an upper bound.

This pattern is actually successfully applied throughout the codebase (especially in the compiler). The parser, besides an AST, collects a bunch of statistics: a number of function definitions in the whole tree, number of variables in each function, number of `break` and `continue` statements in the body of a loop or a switch and so on. This statistics allow different pipeline stages to allocate fixed size arrays upfront, greatly simplifying the code without incurring performance penalties for growing heap allocated arrays.

This, for example, allows the driver to reserve an array of `LoweredFunction` upfront for storing results of compiling each function.
