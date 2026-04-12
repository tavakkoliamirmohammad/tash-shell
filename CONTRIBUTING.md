# Contributing to Tash

Thanks for your interest in contributing to Tash! Here's how to get started.

## Getting Started

1. Fork the repo and clone it locally
2. Build the project:
   ```sh
   cmake -B build && cmake --build build
   ```
3. Run tests:
   ```sh
   cmake -B build -DBUILD_TESTS=ON && cmake --build build
   ctest --test-dir build --output-on-failure
   ```
4. Run the shell:
   ```sh
   ./build/tash.out
   ```

## Making Changes

1. Create a branch from `master`: `git checkout -b my-feature`
2. Make your changes
3. Add tests if applicable
4. Make sure all tests pass
5. Commit with a clear message describing what and why
6. Push and open a pull request

## What to Work On

- Check [open issues](https://github.com/tavakkoliamirmohammad/tash-shell/issues) — look for `good first issue` labels
- Bug fixes are always welcome
- New builtins, completions, or shell features
- Documentation improvements
- Performance improvements

## Code Style

- C++17
- 4-space indentation
- Keep functions short and focused
- Follow existing patterns in the codebase

## Reporting Bugs

Open an issue with:
- What you expected to happen
- What actually happened
- Steps to reproduce
- Your OS and architecture

## Questions?

Open a [discussion](https://github.com/tavakkoliamirmohammad/tash-shell/discussions) or an issue — happy to help.
