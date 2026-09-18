# Migration Guide: Jaguar 1.0.x to 1.1.0

Jaguar 1.1.0 preserves backward compatibility with 1.0.x core language features while introducing major improvements.

## Key Changes in 1.1.0
1. **Toolchain Version**: Standardized to `1.1.0` across CLI, diagnostics, and VS Code extension.
2. **Native Formatter**: Use `jag fmt <file>` or `jag fmt --write <file>` instead of extension line-based formatting.
3. **Native Linter**: Static check with `jag lint <file>` (codes `JAG001` - `JAG010`).
4. **Structured Diagnostics**: Support for `--format=json` across CLI commands.
5. **Project Manifest**: Native support for `jaguar.toml` project files.
