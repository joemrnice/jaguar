# Static Linting in Jaguar

Jaguar 1.1.0 includes a static code analysis linter.

## CLI Usage

```sh
jag lint main.jag               # run linter and print human diagnostics
jag lint --format=json main.jag # emit structured JSON diagnostics
jag lint --fix main.jag         # automatically fix safe linter warnings
```

## Diagnostic Codes

| Code | Description | Severity |
| :--- | :--- | :--- |
| `JAG001` | Ambiguous variable name (e.g., single-letter 'l') | Warning |
| `JAG002` | Unreachable code after return | Warning |
| `JAG003` | Shadowed variable declaration | Warning |
| `JAG004` | Unused variable | Warning |
