# ZScript VS Code Extension — Packaging & Publishing Guide

## Prerequisites

```bash
npm install -g @vscode/vsce   # packaging tool
node -v                        # requires Node.js 18+
```

---

## 1. Build the Extension

```bash
cd vscode-extension
npm install
npm run compile          # compiles src/extension.ts → out/extension.js
```

The `compile` script is defined in `package.json` as `tsc -p ./`.

---

## 2. Test Locally (without publishing)

### Option A — Install the `.vsix` package

```bash
cd vscode-extension
vsce package             # produces zscript-0.2.0.vsix
code --install-extension zscript-0.2.0.vsix
```

Reload VS Code. The extension is now active for any `.zs` file.

### Option B — Run in Extension Development Host

1. Open `vscode-extension/` as the workspace in VS Code.
2. Press `F5`. A new VS Code window opens with the extension loaded live.
3. Open any `.zs` file in that window to test syntax highlighting, diagnostics, and IntelliSense.

---

## 3. Configure the Language Server Path

The extension automatically searches for the `zsc` binary in these locations (in order):

| Location | Example |
|---|---|
| `zscript.serverPath` setting | `/path/to/zsc` |
| Workspace `build/Debug/zsc.exe` | Windows CMake Debug build |
| Workspace `build/Release/zsc.exe` | Windows CMake Release build |
| Workspace `build/zsc` | macOS/Linux CMake build |
| `PATH` | `zsc` on system PATH |

To set a custom path, add to `.vscode/settings.json`:

```json
{
  "zscript.serverPath": "/absolute/path/to/zsc"
}
```

### Building `zsc` for each platform

**macOS / Linux**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
# binary: build/zsc
```

**Windows (MSVC)**
```powershell
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
# binary: build\Release\zsc.exe
```

**Windows (MinGW)**
```powershell
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
# binary: build\zsc.exe
```

---

## 4. Publish to the VS Code Marketplace

### 4.1 Create a Publisher Account

1. Go to [marketplace.visualstudio.com/manage](https://marketplace.visualstudio.com/manage)
2. Sign in with a Microsoft account.
3. Click **Create publisher**. Choose a publisher ID (e.g. `yourname`).

### 4.2 Create a Personal Access Token (PAT)

1. Go to [dev.azure.com](https://dev.azure.com) → your organization → **User Settings → Personal Access Tokens**.
2. Click **New Token**:
   - **Name**: `vsce-publish`
   - **Organization**: All accessible organizations
   - **Scopes**: `Marketplace → Manage`
3. Copy the token — you will only see it once.

### 4.3 Update `package.json`

Make sure these fields are filled in before publishing:

```json
{
  "name": "zscript",
  "displayName": "ZScript",
  "publisher": "YOUR_PUBLISHER_ID",
  "version": "0.2.0",
  "description": "ZScript language support: syntax highlighting, diagnostics, IntelliSense, go-to-definition",
  "repository": {
    "type": "git",
    "url": "https://github.com/YOUR_USERNAME/zscript"
  },
  "license": "MIT"
}
```

> The `publisher` field must exactly match the publisher ID you created in step 4.1.

### 4.4 Add a README and icon (optional but recommended)

- `README.md` — shown on the marketplace page. Include a screenshot and feature list.
- `icon.png` — 128×128 PNG, referenced in `package.json` as `"icon": "icon.png"`.

### 4.5 Package and publish

```bash
cd vscode-extension

# Login once (prompts for your PAT)
vsce login YOUR_PUBLISHER_ID

# Publish directly
vsce publish

# Or: package first, inspect, then publish
vsce package
vsce publish --packagePath zscript-0.2.0.vsix
```

After publishing, the extension appears at:
`https://marketplace.visualstudio.com/items?itemName=YOUR_PUBLISHER_ID.zscript`

---

## 5. Update an Existing Version

Bump the version in `package.json`, then:

```bash
vsce publish patch    # 0.2.0 → 0.2.1  (bug fixes)
vsce publish minor    # 0.2.0 → 0.3.0  (new features)
vsce publish major    # 0.2.0 → 1.0.0  (breaking changes)
```

---

## 6. Install from `.vsix` on another machine

```bash
code --install-extension zscript-0.2.0.vsix
```

Or in VS Code: **Extensions panel → `...` menu → Install from VSIX...**

---

## 7. Uninstall

```bash
code --uninstall-extension YOUR_PUBLISHER_ID.zscript
```

---

## Troubleshooting

| Problem | Fix |
|---|---|
| `spawn zsc ENOENT` | Set `zscript.serverPath` in settings, or add `zsc` to PATH |
| Language server won't start on Windows | Build `zsc` for Windows (see §3); the `.exe` extension is handled automatically |
| Ctrl+Click (go-to-definition) not working | See below |
| Diagnostics not appearing | Run `ZScript: Show Output` command to check LSP logs |

### Go-to-definition keybindings by platform

| Platform | Key |
|---|---|
| macOS | `Cmd+Click` or `F12` |
| Windows / Linux | `Ctrl+Click` or `F12` |

If `Ctrl+Click` on Windows opens multi-cursor instead of going to definition, the `editor.multiCursorModifier` setting may be set to `ctrlCmd`. Change it:

```json
// .vscode/settings.json
{
  "editor.multiCursorModifier": "alt"
}
```

This restores `Ctrl+Click` for go-to-definition and moves multi-cursor to `Alt+Click`.
