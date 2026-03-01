# Sleepy C Implementation

This project is a C implementation of a parser, lexer, and eventually a VM for the `sleepy` programming language.

## Design Constraints

1. **Zero Dependencies**: The core C implementation has absolutely zero external dependencies. It does not even rely on standard `malloc` and `free` implicitly. 
2. **Allocator Approach**: All memory allocations are driven through a custom `SleepyAllocator` which involves a `userdata` pointer and a user-provided reallocation function. This allocator must be passed around everywhere.
3. **Architecture Reference**: The implementation structure takes inspiration from the [Wren](https://github.com/wren-lang/wren) C implementation, while the language semantics and lexer/parser logic are based on the [Sleepy](https://github.com/sleepy-lang/sleepy) reference implementation.

## Project Structure

- `include/sleepy/`: Public headers for incorporating the Sleepy C API into your applications.
- `src/`: Core implementation files for the Lexer, Parser, and VM.
- `tests/`: Unit tests written in C++ using `doctest.h` to verify the C implementation against the standard test fixtures from the `sleepy` reference implementation.

## Testing

Tests are written in C++ to leverage the `doctest.h` unit testing framework, but they test the C implementation. Test fixtures are located in `tests/fixtures/`.

To run the tests:

```bash
make test
```
