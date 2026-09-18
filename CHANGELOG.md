# Changelog

All notable changes to the Jaguar programming language and toolchain will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - 2025-01-15

### Added
- **Native Formatter (`jag fmt`)**: Token and AST-aware C code formatter supporting `--check`, `--write`, and project-wide formatting.
- **Native Linter (`jag lint`)**: Static analysis engine with stable rule diagnostic codes (`JAG001` - `JAG010`), `--format=json`, and automatic safe fixes (`--fix`).
- **Structured Diagnostics**: Machine-readable JSON diagnostic pipeline (`--format=json`) for compiler, type checker, linter, and VS Code extension integration.
- **Project & Module System (`jaguar.toml`)**: Formal project manifest system and relative/root module import resolution with circular dependency detection.
- **Core Language Features**:
  - Struct construction and real struct value instantiation (`Point { x: 10, y: 20 }`).
  - Enum value member access (`Color.Red`) and `match` / pattern matching statements.
  - Parameterized/generic types (`List<T>`, `Map<K, V>`).
  - Static class members (`static var`, `static fun`).
  - Interfaces (`interface`) and `implements` conformance checking.
  - Enforced `public` and `private` access modifiers.
  - Local type inference and type aliases (`type Point2D = Point`).
- **Standard Library Namespaces**: Modular stdlib namespaces (`jag.io`, `jag.fs`, `jag.path`, `jag.net`, `jag.http`, `jag.json`, `jag.time`, `jag.math`, `jag.collections`, `jag.env`, `jag.process`, `jag.crypto`, `jag.test`).
- **Enhanced Concurrency & Networking**: Full WebSocket client (`socket.connect()`), HTTP keep-alive, chunked transfer encoding, and async `Task` cancellation/timeouts.
- **VS Code Extension 2.0**: Updated extension consuming `jag` JSON diagnostics and native formatter directly.

### Changed
- Standardized toolchain and language release version to `1.1.0`.
- Standardized VS Code extension version to `1.1.0`.

### Compatibility
- Full backward compatibility preserved for Jaguar 1.0.x core language source programs. Legacy top-level builtins remain supported as alias mappings to the new `jag.*` standard library namespaces.
