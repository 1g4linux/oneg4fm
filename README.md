# oneg4fm

`oneg4fm` is a Qt-based desktop file manager for Linux/Unix users who want normal file browsing plus built-in archive and binary/hex tooling.  
It is not a headless batch file-operations CLI, and this build currently does not provide trash-backed deletes.

## 1. Name + one-line purpose

`oneg4fm` opens, navigates, and manages local/remote paths with a GUI-first workflow and DBus single-instance behavior.  
It deliberately does not try to be a scripted replacement for tools like `cp`, `mv`, `rsync`, or shell pipelines.

## 2. Why this exists

The project solves a practical desktop problem: one tool for day-to-day file management that still handles advanced cases (archive extraction/writing and binary inspection) without switching apps constantly.

Real scenario: you receive a compressed bundle, inspect its contents, extract safely, and verify resulting files from one UI session. You do not need separate archive and hex tools for the common path.

## 3. Quickstart (fastest path to success)

Minimal source build:

```bash
# TODO: add distro-specific package install lines for your target platform(s).
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Minimal hello-world usage:

```bash
# In headless sessions (CI/SSH), use offscreen for help output.
QT_QPA_PLATFORM=offscreen ./build/oneg4fm/oneg4fm --help
./build/oneg4fm/oneg4fm "$HOME"
```

Expected output shape for `--help`:

```text
Usage: ./build/oneg4fm/oneg4fm [options] [FILE1, FILE2,...]
Options:
  -p, --profile <PROFILE>
  -d, --daemon-mode
  -q, --quit
  -n, --new-window
  -f, --find-files
  --show-pref <NAME>
```

What just happened: the first command confirms the binary and CLI surface; the second opens a window rooted at your home directory.

## 4. Common workflows

1. Open specific paths (local or URI)
Goal: jump directly to a target path.
Command:
```bash
oneg4fm /etc
oneg4fm smb://server/share
```
What to look for: no required terminal output; a window/tab opens on the target location.
Common mistake + fix: using a relative path from the wrong shell directory. Run `pwd` first or pass an absolute path.

2. Run one persistent instance and control it
Goal: keep one process and forward later opens to it.
Command:
```bash
oneg4fm --daemon-mode
oneg4fm ~/Downloads
oneg4fm --quit
```
What to look for: daemon mode keeps the process alive even after windows close; later invocations forward over DBus.
Common mistake + fix: expecting `--profile` on later invocations to switch profile while a daemon is already running. Stop the daemon first (`oneg4fm --quit`) and restart with the desired profile.

3. Use isolated profiles
Goal: separate settings/layouts by context (for example, work vs personal).
Command:
```bash
oneg4fm --profile work ~/src
oneg4fm --profile personal ~/Downloads
```
What to look for: profile-specific settings under separate config directories.
Common mistake + fix: launching a second profile while another instance is already primary. Quit the running instance, then start the profile you want first.

4. Jump directly to utility dialogs
Goal: open search or a specific Preferences page without manual navigation.
Command:
```bash
oneg4fm --find-files ~/src
oneg4fm --show-pref advanced
```
What to look for: Find Files dialog or Preferences opened to the requested page.
Common mistake + fix: invalid `--show-pref` value. Use one of `behavior|display|ui|thumbnail|volume|advanced`.

## 5. Concepts you must understand

Single-instance dispatch over DBus: one process owns the app service; later launches usually forward requests instead of creating independent state.  
Wrong assumption: every CLI invocation is a new isolated instance.  
Correction: a running primary instance handles most requests, including open actions.

Profiles are filesystem directories, not in-memory presets: a profile maps to its own config directory (`.../oneg4fm/<profile>/`).  
Wrong assumption: profile only changes appearance temporarily.  
Correction: profile changes where persistent settings are read/written.

Archive extraction is intentionally strict: extraction rejects unsafe archive paths (`..`, absolute paths), requires a non-existing destination root, and cleans up destination on failure/cancel paths.  
Wrong assumption: extraction merges into existing directories like many archive tools.  
Correction: choose a fresh destination directory and treat rejection as a safety signal, not random failure.

Delete semantics are build-dependent: settings include trash-related keys, but this build hard-disables trash support at runtime.  
Wrong assumption: setting `UseTrash=true` guarantees recoverable deletes.  
Correction: treat deletes as permanent and keep confirmations enabled.

## 6. Configuration

Primary config file:

`~/.config/oneg4fm/<PROFILE>/settings.conf`

Important options (defaults and semantics):

- `[Behavior] ConfirmDelete=true`: prompts before destructive delete.
- `[Behavior] SingleWindowMode=false`: when true, reuses an existing main window where possible.
- `[Window] ReopenLastTabs=false`: when true, restores previous tabs on startup (profile-local).
- `[Window] PathBarButtons=true`: breadcrumb button mode in the path bar.
- `[FolderView] ShowHidden=false`: show dotfiles and other hidden entries.
- `[Thumbnail] ShowThumbnails=true`: render thumbnails when supported.
- `[System] Terminal`: preferred terminal command (`qterminal` in default template, fallback to `xterm` if unset).

Safe config example:

```ini
[Behavior]
ConfirmDelete=true
QuickExec=false

[Window]
ReopenLastTabs=false
SingleWindowMode=false
```

Dangerous config example:

```ini
[Behavior]
ConfirmDelete=false
QuickExec=true
```

Why dangerous: accidental execution and accidental permanent delete become much easier.

## 7. Operational notes

Performance behavior that affects real use:

- Copy/move/delete progress is pre-scanned before execution to compute stable totals. Large trees can show a short "planning" delay before meaningful progress.
- Core copy/extract paths are streaming implementations (they do not load entire files into memory by default).
- Cancellation is cooperative and surfaced as `ECANCELED`/"Operation cancelled" behavior in core/backend paths.

Logging and diagnostics:

- Run from a terminal to see warnings/debug output.
- For verbose Qt debug categories:
```bash
QT_LOGGING_RULES="*.debug=true" oneg4fm /path
```

Where state lives:

- Main profile settings: `~/.config/oneg4fm/<PROFILE>/settings.conf`
- Per-folder overrides: `~/.config/oneg4fm/<PROFILE>/dir-settings.conf`
- User directories input watched by app: `~/.config/user-dirs.dirs`
- Qt `QSettings` stores (file dialog / mount dialog) under organization `oneg4fm` (exact file path depends on Qt platform backend; typically under `~/.config/` on Linux).

Settings conflict/precedence rules:

- Effective value precedence is: explicit directory key (`.directory`) > profile key (`settings.conf`) > schema default.
- Directory overrides are scalar overrides only; missing directory keys inherit profile effective values.
- When the same key appears multiple times in a single config file, last write wins.
- List keys are replace semantics (not append) unless a future schema key explicitly documents otherwise.

## 8. Troubleshooting

Symptom: `--profile` seems ignored, but windows still open normally.  
Likely cause: a different profile instance is already primary, and your command was forwarded to it.  
How to confirm: `ps -ef | rg oneg4fm` and check that a process already exists.  
Fix: `oneg4fm --quit`, then start again with `--profile <name>` first.

Symptom: running `oneg4fm --help` fails or exits unexpectedly in CI/SSH/headless sessions.  
Likely cause: no GUI/display backend available.  
How to confirm: `echo "$DISPLAY"` is empty and no Wayland display is configured.  
Fix: use `QT_QPA_PLATFORM=offscreen oneg4fm --help` for CLI help checks.

Symptom: delete acts permanent even though `UseTrash=true` exists in config.  
Likely cause: this build disables trash support at runtime.  
How to confirm: behavior remains permanent despite config edits.  
Fix: keep `ConfirmDelete=true`; do not rely on trash recovery in this build.

Symptom: archive extraction fails with "Destination already exists" or unsafe path errors.  
Likely cause: extraction policy requires a fresh destination and rejects path-traversal entries.  
How to confirm: target folder already exists, or archive contains entries like `../name` or absolute paths.  
Fix: extract into a new directory and re-check archive integrity/content.

Symptom: file operation reports "Operation cancelled."  
Likely cause: operation was canceled (UI/worker cancellation path).  
How to confirm: progress stops early and final status is cancellation, not a filesystem error.  
Fix: rerun without canceling; for recurring cancels, check input size/tree depth and interaction timing.

## 9. Limitations and non-goals

- No first-class non-interactive CLI for bulk file operations; GUI workflows are primary.
- Trash-backed delete is not active in this build.
- `FileOpRequest` contains flags like `followSymlinks`/`overwriteExisting`, but current backend behavior is primarily driven by core safe defaults, not a user-facing toggle surface.
- Platform scope is POSIX-oriented code paths; this is not a Windows-focused file manager.
- TODO: roadmap/intent for trash re-enable and user-facing overwrite/follow-symlink controls.

## 10. Compatibility & stability

- Build requirements in this tree: `CMake >= 3.18`, `Qt >= 6.6`.
- Core implementation relies on POSIX syscalls (`openat`, `fstatat`, `utimensat`, etc.); treat Linux/Unix-like systems as the intended runtime target.
- CLI options are stable enough for user invocation (`--profile`, `--daemon-mode`, `--quit`, `--new-window`, `--find-files`, `--show-pref`), but no strict semantic-versioning policy is documented yet.
- Config format is INI (`settings.conf`), with unknown keys generally ignored and missing keys defaulted.
- TODO: define explicit compatibility promises (settings schema and automation guarantees) if you want downstream packagers/scripts to rely on them.
