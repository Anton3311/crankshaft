<h1>
    <p align="center">Crankshaft</p>
</h1>

<p aling="center">
    <p align="center">
        <a href="docs/architecture.md">Architecture</a>
        ·
        <a href="#how-to-build">Building</a>
        ·
        <a href="docs/design.md">Design</a>
        ·
        <a href="docs">Docs</a>
        ·
        <a href="#running-tests">Testing</a>
    </p>
</p>

# About

`Crankshaft` is a compiler for a subset of C99 with a custom Sea of Nodes Intermediate Representation and a custom x64 backend.

Written fully in C with only dependecies being `Tracy` profiler and a C compiler.

`Crankshaft` is already in a state, where it is capable of self-hosting parts of itself. For now is only the tokenizer (`parser/src/parser/tokenizer.c`), see the test `tester/src/tester/self_hosting/self_hosted_tokenizer_driver.c`.

> [!IMPORTANT]
> Not fully standard complient

> [!NOTE]
> Doesn't produce an executable (yet), rather it runs the program in the same process as the compiler.

---

Features:

1. `x64` machine code generation
2. `x64` calling convention on Windows
3. Support for using structs as function argument and return types.
4. Calling of external functions. These are provided internally as function pointers by the compiler.
5. A custom [linker](code_gen/src/code_gen/backends/x64_linker.c) that enables compilation of multiple functions and source files into a single program.
5. `char`, `int`, `short`, `long`, `long long` and their signed/unsigned variats with support for all binary and unary operators.
6. Integer and pointer arithmetics.
7. Pointer dereferencing and assignment.
8. Array indexing and element assignment
9. Conditional branches
10. Switch statements
10. Comparison operators: `==`, `!=`, `<`, `<=`, `>`, `>=` and unary not `!`.
11. `while`, `for` and `do while` loops
10. String constants
11. Macros: regular and function-like
12. Conditional preprocessor directives
13. `#include`, `#error`, `#undef`, `#pragma once`
14. Builtin macros: `__LINE__`, `__FILE__` and `__STDC__`
15. Macros with variable number of arguments (`__VA_ARGS__`)

# How to build

The project is Windows only and can be built using both Clang and MSVC.

The build process produces multiple executables:
- `bin/c.exe` - the compiler
- `bin/test_runner.exe` - a test runner
- `bin/tester.exe` - an exe that actually runs the tests. **Not meant for manual use**. It is only lauched by `test_runner.exe` and its main purpose is to isolate the tests so that in case of a crash the `test_runner` can keep on running other tests.
- `bin/gen.exe` - source code generator, currently only used to generate `code_gen/src/code_gen/instr.gen.c`.

By default the build tool looks for the selected compiler in the `PATH`, however it is also possible to override the search path, by specifying a `;` separated list of paths with the `--compiler-paths=<search-paths>` option.

## Building using clang

To build the project first you need to compile the build tool:

```
.\scripts\build_bb.bat clang
```

Then run the build tool:

```
.\bin\bb.exe build
```

## Building using MSVC

Open the `Developer PowerShell` or `Developer Command Prompt` and run the following command, to compile the build tool:

```
.\scripts\build_bb.bat cl
```

Then run the build tool:

```
.\bin\bb.exe build --cc=cl
```

## Asan

When building with `MSVC` it is possible to compile with address sanitization, by passing `--asan` flag to the build tool.

> [!NOTE]
> `Asan` is `MSVC` only, since `clang`'s implementation is rather buggy.

## Profiling

`Crankshaft` uses `Tracy 0.10.0` as the profiler.

Building with the profiler support requires only passing the `--profiler` flag to the build tool.

> [!NOTE]
> All the required `Tracy` client code is already included in the repository, so it doesn't require any extra steps.
> 
> Compiling with profiler support will work even if you don't have `Tracy` installed, however you will need it to view the profiling results.
> 
> Tracy 0.10.0 release can be found here: https://github.com/wolfpld/tracy/releases#release-v0.10

# Running the compiler

```
.\bin\c.exe <path-to-your-c-file>
```

```
  Usage:
    c.exe <path-to-c-file>

  Compiler flags:
    --no-win-sdk           don't add Win SDK to include path
    -I<include-path>       specify an include path
    --show-ast             print AST after parsing

  Backend flags:
    --show-ir              print generated IR instructions
    --x64-debug-log        log results of intermediate operations for debugging
    --x64-show-instr-loc   print which storage locations were assigned to each instruction
```

# Running tests

Use `.\bin\test_runner.exe` to run all the tests:

```
.\bin\test_runner.exe
```

It is also possible to run only a specific test:
```
.\bin\test_runner.exe run-test <test-name>
```

The test runner also integrates with RAD Debugger. RAD Debugger is not required for the compiler or the test runner to function. However, it unlocks extra features, that mostly come in handy during development.

Other options:

1. `--stop-on-fail` - stop after the first failing test
2. `--debug-failed` - automatically add failing tests as targets to RAD Debugger.
3. `--raddbg-path=<path>` - explicitely specify the path to the `raddbg.exe`. By default test runner will look for `raddbg.exe` in the `PATH`.
