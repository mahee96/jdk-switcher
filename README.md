# 🔀 jdk-switcher

A lightweight command-line tool to switch between multiple installed JDKs using a JSON config file. This utility updates your `JAVA_HOME`, ensures the selected JDK is in your `PATH`, and persists the setup via your shell profile.

------

## ⚙️ Setup

### 1. Install dependencies

You’ll need:

- `gcc` (typically comes with Xcode or Xcode Command Line Tools)
- [`cJSON`](https://github.com/DaveGamble/cJSON) (install via Homebrew: `brew install cjson`)

### 2. Build

```sh
CJSON_DIR=$(brew --prefix cjson)
gcc -o build/jdk jdk.c -I"$CJSON_DIR/include" -L"$CJSON_DIR/lib" -lcjson

```

------

## 📁 Configuration

Create a `config.json` in the same directory as the binary:

```json
{
    "command": "/usr/libexec/java_home -V 2>&1 | sed -nE 's/.* (\/.*)$/\\1/p'",
    "symlink_path": "~/.devtools/jdk",
    "profile_file_path": "~/.zshrc",
    "jdks": [
        "~/Library/Java/JavaVirtualMachines/corretto-21.0.3/Contents/Home",
        "~/Library/Java/JavaVirtualMachines/corretto-17.0.11/Contents/Home",
        "~/Library/Java/JavaVirtualMachines/corretto-11.0.23/Contents/Home",
        "~/Library/Java/JavaVirtualMachines/corretto-1.8.0_412/Contents/Home"
    ]
}

```

------

## 🚀 Usage

### List all configured JDKs

```
./jdk --list
```

### Switch to a JDK by index

```sh
./jdk --use 2
```

This:

- Creates a symlink at `~/.devtools/jdk` pointing to the selected JDK
- Exports it as `JAVA_HOME` by updating the ~/.zshrc
- Adds it to your `PATH` (if not already)
- Reloads your shell environment

------

## 💡 Notes

- Only tested with `zsh`, but should work with any shell that supports sourcing profile files and `SIGUSR1` trap.
- If you modify `config.json`, just re-run `./jdk --use <index>` to apply changes.
