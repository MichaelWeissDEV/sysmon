# Contributing

We welcome contributions! This guide explains how to set up your development environment and submit changes.

## Development Setup

```bash
git clone https://github.com/MichaelWeissDEV/sysmon.git
cd sysmon
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

## Running Tests

```bash
cd build && ctest --output-on-failure
```

## Code Style

- C++20 standard
- `snake_case` for functions and variables
- `PascalCase` for classes and structs
- All public API must have Doxygen comments
- No external runtime dependencies (system libraries only)
- Each monitor must compile cleanly on both Linux and macOS

## Adding a New Monitor

See the [Architecture](../architecture) page for a step-by-step guide.

## Submitting a Pull Request

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Write code and tests
4. Ensure `ctest` passes
5. Submit a pull request with a clear description

## Reporting Issues

Please open a GitHub issue with:
- OS and kernel version
- CPU/hardware details (for sensor issues)
- Steps to reproduce
- Expected vs. actual output
