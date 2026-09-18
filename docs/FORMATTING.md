# Code Formatting in Jaguar

Jaguar 1.1.0 provides a native, token-aware code formatter built directly into the `jag` CLI.

## CLI Usage

```sh
jag fmt main.jag               # format main.jag to stdout
jag fmt --write main.jag       # format main.jag in place
jag fmt --check main.jag       # check if main.jag complies with format
```

## Idempotency Guarantee
Jaguar's native formatter is idempotent:
`format(format(source)) == format(source)`

## Project Configuration (`jaguar.toml`)

```toml
[format]
indent_width = 4
use_tabs = false
line_width = 100
```
