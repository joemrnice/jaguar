# Jaguar Language Support for VSCode

[![Version](https://img.shields.io/badge/version-1.0.0-blue)](https://marketplace.visualstudio.com/items?itemName=jaguar-lang-team.jaguar-lang)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

🐆 **Official VSCode extension for the Jaguar programming language**

This extension provides comprehensive IDE support for Jaguar (`.jag` and `.ja` files), including syntax highlighting, linting, formatting, live reload, and seamless integration with the `jag` CLI toolchain.

![Jaguar Extension Demo](https://via.placeholder.com/800x450/FF6B35/FFFFFF?text=Jaguar+Language+Support+Demo)

## Features

### ✨ Syntax Highlighting
Full color highlighting for all Jaguar language constructs:
- Keywords (`var`, `fixed`, `fun`, `class`, `if`, `elif`, `else`, etc.)
- Types (`string`, `num`, `decimal`, `bool`, `data`, `list<T>`, `MixedList`)
- Operators (arithmetic, comparison, logical, ternary, range `<<<`)
- Comments (single-line `//` and block `/* */`)
- String interpolation (`{{ }}` and `${ }`)
- Built-in modules (`live`, `file`, `dir`, `env`, `json`, `http`, `socket`, `worker`, `server`)

### 🔍 Linting & Error Detection
Real-time error detection using `jag check`:
- Type errors displayed inline with red squiggles
- Parse errors highlighted in the Problems panel
- Automatic linting on file save (configurable)
- One-click lint command from editor title bar

### 📝 Code Formatting
Built-in formatter for consistent code style:
- Automatic indentation (4 spaces per level)
- Brace-aware formatting
- Trigger via command or on save (configurable)
- Keyboard shortcut: `Ctrl+Shift+I` (Windows/Linux) / `Cmd+Shift+I` (macOS)

### 🔄 Live Reload
Watch mode for rapid development:
- Toggle with one click in the status bar
- Automatically re-runs your code on every file save
- Perfect for iterative development and testing
- Status bar indicator shows when enabled

### 🚀 Run & Build Commands
Integrated execution directly from VSCode:
- **Run File**: Execute current Jaguar file with `jag run`
- **Check File**: Run type checker with `jag check`
- **Build Project**: Compile to native code with `jag build` (when available)
- Output displayed in dedicated "Jaguar" output panel

### 🎯 Quick Actions
Access commands from multiple places:
- Editor title bar icons (Run, Check, Live Reload)
- Right-click context menu in file explorer
- Command Palette (`Ctrl+Shift+P` / `Cmd+Shift+P`)
- Custom keyboard shortcuts

## Installation

### Prerequisites

Before installing the extension, ensure you have the Jaguar toolchain installed:

#### Option 1: Install from Source (Recommended for Development)

```bash
# Clone the Jaguar repository
git clone https://github.com/jaguar-lang/jaguar.git
cd jaguar

# Build the jag binary
make

# Install to /usr/local/bin (requires sudo)
sudo ./install.sh

# Or install to a custom location (no sudo needed)
PREFIX=$HOME/.local ./install.sh
```

#### Option 2: Verify Installation

After installation, verify `jag` is in your PATH:

```bash
jag --version
jag --help
```

You should see version information and CLI usage details.

### Installing the Extension

#### Method 1: From VSIX Package (Local Installation)

1. **Download the extension package** (`.vsix` file) from the releases page

2. **Install in VSCode**:
   - Open VSCode
   - Go to Extensions view (`Ctrl+Shift+X` / `Cmd+Shift+X`)
   - Click the `...` menu at the top
   - Select "Install from VSIX..."
   - Choose the downloaded `.vsix` file
   - Click "Install"

3. **Reload VSCode** when prompted

#### Method 2: From Marketplace (When Published)

1. Open VSCode
2. Go to Extensions view (`Ctrl+Shift+X` / `Cmd+Shift+X`)
3. Search for "Jaguar Language"
4. Click "Install" on the extension by `jaguar-lang-team`
5. Reload VSCode when prompted

#### Method 3: Manual Installation (Development)

1. **Clone the extension repository**:
   ```bash
   git clone https://github.com/jaguar-lang/jaguar-vscode-extension.git
   cd jaguar-vscode-extension
   ```

2. **Install dependencies**:
   ```bash
   npm install
   ```

3. **Compile TypeScript**:
   ```bash
   npm run compile
   ```

4. **Run in development mode**:
   - Press `F5` in VSCode to launch an Extension Development Host
   - The extension will be active in the new window

## Usage Guide

### Getting Started

1. **Open a Jaguar project folder** in VSCode:
   ```bash
   code /path/to/your/jaguar/project
   ```

2. **Create or open a `.jag` or `.ja` file**

3. **The extension activates automatically** when you open a Jaguar file

### Running Jaguar Code

#### From the Editor Title Bar
Click the ▶️ **Run** icon in the top-right of the editor:

![Run Icon Location](https://via.placeholder.com/400x100/2D2D2D/FFFFFF?text=Editor+Title+Bar:+%E2%96%B6+%E2%9C%93+%E2%9F%BA)

#### From Context Menu
Right-click a `.jag` file in the Explorer and select **"Run Jaguar File"**

#### Using Keyboard Shortcut
- **Windows/Linux**: `Ctrl+Shift+F5`
- **macOS**: `Cmd+Shift+F5`

#### From Command Palette
1. Press `Ctrl+Shift+P` (Windows/Linux) or `Cmd+Shift+P` (macOS)
2. Type "Jaguar: Run"
3. Press Enter

### Linting (Error Checking)

#### From the Editor Title Bar
Click the ✓ **Check** icon to run the type checker:

![Check Icon Location](https://via.placeholder.com/400x100/2D2D2D/FFFFFF?text=Editor+Title+Bar:+%E2%96%B6+%E2%9C%93+%E2%9F%BA)

Errors appear as:
- **Red squiggly underlines** in the editor
- **Problems panel** entries (View → Problems)
- **Output channel** messages

#### Keyboard Shortcut
- **Windows/Linux**: `Ctrl+Shift+L`
- **macOS**: `Cmd+Shift+L`

### Formatting Code

#### From Command Palette
1. Press `Ctrl+Shift+P` / `Cmd+Shift+P`
2. Type "Jaguar: Format"
3. Press Enter

#### Keyboard Shortcut
- **Windows/Linux**: `Ctrl+Shift+I`
- **macOS**: `Cmd+Shift+I`

#### Format on Save
Enable automatic formatting in settings:
```json
{
  "jaguar.formatOnSave": true
}
```

### Live Reload Mode

Perfect for rapid iteration during development!

#### Toggle from Status Bar
Click the **⟳ Live Reload** indicator in the bottom-right status bar:

![Status Bar](https://via.placeholder.com/600x50/2D2D2D/FFFFFF?text=Status+Bar:+Main++Line+Col++UTF-8++LF++Spaces:+4++JSON++⟳+Live+Reload)

When enabled:
- Icon changes to spinning ⟳~spin
- Background turns warning color (orange/yellow)
- Every file save triggers automatic re-execution

#### Keyboard Shortcut
- **Windows/Linux**: `Ctrl+Shift+R`
- **macOS**: `Cmd+Shift+R`

#### Enable in Settings
To enable live reload by default:
```json
{
  "jaguar.enableLiveReload": true,
  "jaguar.lintOnSave": true
}
```

### Configuration Options

Access settings via:
- **File → Preferences → Settings** (Windows/Linux)
- **Code → Preferences → Settings** (macOS)
- Keyboard: `Ctrl+,` (Windows/Linux) or `Cmd+,` (macOS)

Search for "jaguar" to see all available options:

| Setting | Default | Description |
|---------|---------|-------------|
| `jaguar.executablePath` | `"jag"` | Path to the `jag` executable. Change if not in PATH |
| `jaguar.enableLiveReload` | `false` | Enable live reload on file save |
| `jaguar.lintOnSave` | `true` | Run linter automatically when saving |
| `jaguar.formatOnSave` | `false` | Format document automatically on save |
| `jaguar.showOutputOnRun` | `true` | Show output terminal when running code |

#### Example `settings.json`

```json
{
  "jaguar.executablePath": "/usr/local/bin/jag",
  "jaguar.enableLiveReload": true,
  "jaguar.lintOnSave": true,
  "jaguar.formatOnSave": true,
  "jaguar.showOutputOnRun": false
}
```

### Working with the Output Panel

The extension uses a dedicated **Jaguar** output channel:

1. **View → Output** (or `Ctrl+Shift+U` / `Cmd+Shift+U`)
2. Select **"Jaguar"** from the dropdown

The output panel shows:
- Execution results and program output
- Lint errors and warnings
- Build progress and errors
- Live reload notifications

## Keyboard Shortcuts Reference

| Action | Windows/Linux | macOS |
|--------|---------------|-------|
| Run File | `Ctrl+Shift+F5` | `Cmd+Shift+F5` |
| Check/Lint | `Ctrl+Shift+L` | `Cmd+Shift+L` |
| Format Document | `Ctrl+Shift+I` | `Cmd+Shift+I` |
| Toggle Live Reload | `Ctrl+Shift+R` | `Cmd+Shift+R` |

## Troubleshooting

### Extension Not Activating

**Problem**: Extension doesn't activate when opening `.jag` files

**Solutions**:
1. Ensure file has `.jag` or `.ja` extension
2. Check if extension is enabled: Extensions view → search "Jaguar" → enable
3. Reload VSCode: `Ctrl+Shift+P` → "Developer: Reload Window"

### "jag command not found"

**Problem**: Running code fails with "Failed to run Jaguar"

**Solutions**:
1. Verify installation: `jag --version` in terminal
2. Add to PATH if needed (see Installation section)
3. Set custom path in settings:
   ```json
   {
     "jaguar.executablePath": "/home/user/.local/bin/jag"
   }
   ```

### Linting Errors Not Showing

**Problem**: No diagnostics appear despite errors in code

**Solutions**:
1. Check Problems panel (View → Problems)
2. Ensure `jag check` works in terminal
3. Verify `jaguar.lintOnSave` is enabled
4. Check Output panel for error details

### Live Reload Not Working

**Problem**: Code doesn't re-run on save

**Solutions**:
1. Toggle live reload: click status bar item or use shortcut
2. Ensure `jaguar.enableLiveReload` is true in settings
3. Check that file is saved (auto-save may be off)

### Formatting Issues

**Problem**: Code doesn't format correctly

**Solutions**:
1. Try manual format: `Ctrl+Shift+I` / `Cmd+Shift+I`
2. Check for syntax errors (formatter requires valid syntax)
3. Report edge cases as issues

## Example Workflow

Here's a typical development workflow with the extension:

1. **Create a new Jaguar file**: `hello.jag`
   ```jaguar
   var name: string = "World";
   live.on("Hello, {{name}}!");
   ```

2. **Enable Live Reload**: Click the status bar item

3. **Start coding**: Every save automatically runs the code

4. **See errors instantly**: Type errors show with red squiggles

5. **Format before committing**: `Ctrl+Shift+I` to clean up indentation

6. **Check for issues**: `Ctrl+Shift+L` to run full type check

## Contributing

Contributions are welcome! Here's how to help:

### Development Setup

```bash
# Clone the repository
git clone https://github.com/jaguar-lang/jaguar-vscode-extension.git
cd jaguar-vscode-extension

# Install dependencies
npm install

# Compile TypeScript
npm run compile

# Watch for changes during development
npm run watch
```

### Running Tests

```bash
npm test
```

### Submitting Changes

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/amazing-feature`
3. Commit changes: `git commit -m 'Add amazing feature'`
4. Push: `git push origin feature/amazing-feature`
5. Open a Pull Request

### Reporting Issues

Please include:
- VSCode version
- Extension version
- Jaguar CLI version (`jag --version`)
- Operating system
- Steps to reproduce
- Expected vs actual behavior

## License

MIT License - see [LICENSE](LICENSE) file for details

## Acknowledgments

- Jaguar language created by the Jaguar Language Team
- Icons and branding inspired by the majestic jaguar 🐆
- Built with love for developers everywhere

---

**Happy Coding with Jaguar!** 🐆✨

For more information about the Jaguar language itself, see:
- [Jaguar Language Guide](https://github.com/jaguar-lang/jaguar/blob/main/docs/LANGUAGE_GUIDE.md)
- [Jaguar Repository](https://github.com/jaguar-lang/jaguar)
- [Jaguar Examples](https://github.com/jaguar-lang/jaguar/tree/main/examples)
