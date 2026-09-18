# Quick Start Guide - Jaguar Language VSCode Extension

## 🚀 5-Minute Setup

### Step 1: Install the Jaguar Compiler

```bash
# Clone and build Jaguar
cd /workspace
make

# Install to your PATH
sudo ./install.sh
# OR without sudo:
PREFIX=$HOME/.local ./install.sh

# Verify installation
jag --version
```

### Step 2: Install the VSCode Extension

**Option A: From VSIX file (Recommended)**
```bash
# In VSCode:
# 1. Press Ctrl+Shift+X (Cmd+Shift+X on Mac)
# 2. Click "..." menu
# 3. Select "Install from VSIX..."
# 4. Choose: /workspace/jaguar-lang-extension/jaguar-lang-1.0.0.vsix
```

**Option B: Development Mode**
```bash
cd /workspace/jaguar-lang-extension
npm install
npm run compile

# Then press F5 in VSCode to launch extension development host
```

### Step 3: Create Your First Jaguar Program

Create a new file `hello.jag`:

```jaguar
// hello.jag - Your first Jaguar program!
var name: string = "Developer";
live.on("Welcome to Jaguar, {{name}}! 🐆");

var numbers: list<num> = [1, 2, 3, 4, 5];
iterate(numbers, n) {
    live.on("Number: {{n}}");
}
```

### Step 4: Run Your Code

**Method 1**: Click the ▶️ icon in the editor title bar  
**Method 2**: Press `Ctrl+Shift+F5` (or `Cmd+Shift+F5` on Mac)  
**Method 3**: Right-click → "Run Jaguar File"

View output in the "Jaguar" output panel (`Ctrl+Shift+U`).

---

## ✨ Key Features at a Glance

| Feature | How to Use | Shortcut |
|---------|------------|----------|
| **Run Code** | Click ▶️ or right-click | `Ctrl+Shift+F5` |
| **Lint/Check** | Click ✓ icon | `Ctrl+Shift+L` |
| **Format Code** | Command Palette | `Ctrl+Shift+I` |
| **Live Reload** | Click status bar ⟳ | `Ctrl+Shift+R` |

---

## 🎯 Try These Next

### Example 1: Type Checking

```jaguar
// errors.jag - See linting in action!
var x: num = "not a number";  // ← Error highlighted!
live.on(x);
```

Press `Ctrl+Shift+L` to see the error in the Problems panel.

### Example 2: Live Reload

1. Enable Live Reload (click ⟳ in status bar)
2. Modify your code
3. Save (`Ctrl+S`)
4. Watch it re-run automatically!

### Example 3: Classes & OOP

```jaguar
// oop.jag - Real object-oriented programming
class Animal {
    var name: string;
    
    fun constructor(name: string) {
        this.name = name;
    }
    
    fun speak(): string {
        return "{{this.name}} makes a sound";
    }
}

class Dog extends Animal {
    var breed: string;
    
    fun constructor(name: string, breed: string) {
        super(name);
        this.breed = breed;
    }
    
    fun speak(): string {
        return "{{super.speak()}} - WOOF!";
    }
}

var dog: Dog = new Dog("Rex", "Labrador");
live.on(dog.speak());
```

---

## ⚙️ Recommended Settings

Add these to your `.vscode/settings.json`:

```json
{
  "jaguar.lintOnSave": true,
  "jaguar.formatOnSave": true,
  "jaguar.enableLiveReload": false,
  "editor.formatOnSave": false,
  "files.associations": {
    "*.jag": "jaguar",
    "*.ja": "jaguar"
  }
}
```

---

## 🆘 Troubleshooting

**Problem**: "jag command not found"  
**Solution**: 
```bash
# Check if jag is installed
which jag

# If not, add to PATH
export PATH=$HOME/.local/bin:$PATH  # For local install
```

**Problem**: Extension doesn't activate  
**Solution**: 
1. Ensure file has `.jag` or `.ja` extension
2. Reload VSCode: `Ctrl+Shift+P` → "Developer: Reload Window"

**Problem**: No syntax highlighting  
**Solution**: 
1. Check file association: `Ctrl+K M` → Select "Jaguar"
2. Reinstall extension if needed

---

## 📚 Learn More

- **Full Documentation**: See [README.md](README.md)
- **Language Guide**: [/workspace/docs/LANGUAGE_GUIDE.md](../docs/LANGUAGE_GUIDE.md)
- **Examples**: [/workspace/examples/](../examples/)

Happy coding with Jaguar! 🐆✨
