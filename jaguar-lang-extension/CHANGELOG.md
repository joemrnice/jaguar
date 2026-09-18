# Change Log

All notable changes to the Jaguar Language Support extension will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2024-01-01

### Initial Release

#### Features
- **Syntax Highlighting**: Complete color highlighting for all Jaguar language constructs including keywords, types, operators, comments, and string interpolation
- **Linting**: Real-time error detection using `jag check` with inline diagnostics and Problems panel integration
- **Formatting**: Built-in code formatter with automatic indentation and brace-aware formatting
- **Live Reload**: Watch mode that automatically re-runs code on file save with status bar indicator
- **Run Commands**: Integrated execution via editor title bar icons, context menus, and keyboard shortcuts
- **Output Panel**: Dedicated "Jaguar" output channel for execution results and error messages

#### Configuration Options
- `jaguar.executablePath`: Path to the jag executable
- `jaguar.enableLiveReload`: Enable/disable live reload feature
- `jaguar.lintOnSave`: Run linter automatically on file save
- `jaguar.formatOnSave`: Format document automatically on save
- `jaguar.showOutputOnRun`: Show output terminal when running code

#### Keyboard Shortcuts
- `Ctrl+Shift+F5` (Win/Linux) / `Cmd+Shift+F5` (macOS): Run File
- `Ctrl+Shift+L` (Win/Linux) / `Cmd+Shift+L` (macOS): Check/Lint File
- `Ctrl+Shift+I` (Win/Linux) / `Cmd+Shift+I` (macOS): Format Document
- `Ctrl+Shift+R` (Win/Linux) / `Cmd+Shift+R` (macOS): Toggle Live Reload

#### Known Limitations
- Formatter is basic and may not handle all edge cases perfectly
- Build command (`jag build`) requires the AOT backend which is not yet implemented in the core Jaguar toolchain
- Live reload watches the entire workspace; large projects may experience slight delays

---

## Future Roadmap

### Planned Features
- [ ] Advanced code snippets for common patterns
- [ ] Code completion/intellisense powered by the type checker
- [ ] Go to definition and find references
- [ ] Symbol outline view
- [ ] Debug adapter integration
- [ ] Workspace symbols search
- [ ] Refactoring tools (extract function, rename, etc.)
- [ ] Enhanced formatter with configurable options
- [ ] Unit test runner integration
- [ ] Documentation hover tooltips

### Under Consideration
- Integration with Jaguar language server protocol (LSP) implementation
- Performance profiling tools
- Memory usage visualization
- Interactive REPL integration

---

For more information about the Jaguar language itself, visit:
- [Jaguar Language Guide](https://github.com/jaguar-lang/jaguar/blob/main/docs/LANGUAGE_GUIDE.md)
- [Jaguar Repository](https://github.com/jaguar-lang/jaguar)
