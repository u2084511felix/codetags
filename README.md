# Codetags

Codetags is a tool that automatically scans your codebase for special comments and creates a summary file with all the found tags.

## Supported Tags

The tool recognizes these tag types in comments:
- TODO
- FIXME
- NOTE
- WARNING
- WARN
- BUG
- FIX

Tags must be followed by a colon and can appear in any comment style eg:
- Single line comments: // TODO:
- Multi-line comments: /* TODO: */

## Features

- Automatically scans source code files for tags like TODO, FIXME, NOTE, WARNING, BUG, WARN, FIX
- Creates a codetags.md file with all found tags organized by type
- Runs as a background daemon that monitors file changes in real-time
- Supports multiple repositories simultaneously
- Configurable ignore patterns through .ctagsignore files
- Automatically adds unique IDs to tags for tracking
- Updates codetags.md file automatically when tags are added, modified, or removed

## How It Works

The codetags daemon runs in the background and:
- Monitors all registered repositories for file changes
- Scans source files when they are created, modified, or accessed
- Automatically adds unique IDs to tags that don't have them
- Updates the codetags.md file in each repository when changes occur
- Respects .ctagsignore files to skip unwanted files or directories
- Handles file creation, modification, deletion, and renaming events
- WARNING Copy events will replace the existing codetag items in the codetags.md with the new file name due to CT-ID duplication made during the copy (this will be patched in future updates).

## Multiple Repositories

You can initialize codetags in multiple repositories. The daemon will monitor all of them simultaneously. Each repository maintains its own codetags.md file with only its own tags.

## Supported File Types

- C++ (.cpp, .h, .hpp)
- C (.c)
- Java (.java)
- JavaScript (.js)
- TypeScript (.ts)
- Python (.py)
- Ruby (.rb)
- Go (.go)
- Rust (.rs)
- PHP (.php)

## Build & Run:
Install:
`make install`

Uninstall:
`sudo make uninstall`
`make clean`

## Usage

### Initialize Codetags in a Repository

Navigate to your repository directory and run:

`codetags init`

This will:
- Create a codetags.md file in the current directory
- Start the background daemon
- Begin monitoring the current directory for tags

### Add Tags to Your Code

Add special comments in your source files:

// TODO: This needs to be implemented 
// FIXME: This is a temporary fix 
/* NOTE: Important information here */ 

The tool will automatically generate unique IDs for tags that don't have them.
For example:
// FIXME: This is a temporary fix [CT-5E6F7G8H]

### Remove Repository from Monitoring

To stop monitoring the current repository:
`codetags remove`

## Configuration

### Ignore Files

Create a .ctagsignore file in your repository root to specify files or directories to ignore, e.g:

build/
*.log
temp/
node_modules/
*.tmp

Patterns follow standard glob patterns:
- Lines starting with # are treated as comments
- Patterns can match files or directories
- Patterns starting with / are anchored to the repository root
- Patterns ending with / match only directories

### Codetags File

The codetags.md file is automatically generated and updated with the following format:

```
## TODO
- **[CT-1A2B3C4D]** Implement this feature
  - *File:* src/main.cpp:15
  - *Modified:* 2023-10-15 10:30:45
## FIXME
- **[CT-5E6F7G8H]** Temporary workaround
  - *File:* src/utils.cpp:42
  - *Modified:* 2023-10-15 10:35:2
```

## Feature roadmap

### Near-term

- Tag formatting: Support multiple output styles (table, list, minimal).

- Split tags into multiple files: E.g., codetags.md for dev TODOs, and warnings.md for warnings/bugs.

### Mid/long-term
- Ticketing system integration:

- Convert codetags into trackable tickets

- Calendar scheduling for deadlines

- Email notifications for assigned items

- Full autodoc generation:

- Collate codetags + source doc comments into full documentation

## Automation pipelines:

- Make codetag data machine-readable for agentic processes (AI/dev bots)

- Expose as API or message bus for other tools


### Why did I build codetags?
I built this because I was looking for an easy way to find and track lost or untracked todo items within my source code, particularly from old project, or projects with multiple versios or branches.

This solution allows:
- Immediate visibility: Every codetag is tracked in one place, always up‑to‑date.
- Persistent IDs: IDs stay fixed so you can reference them in tickets, commits, or docs.
- Zero manual upkeep: System‑wide watcher means no per‑repo watcher scripts.
- Extensible: Designed to grow into more powerful project management and automation tooling.

### Contributing

Please feel free to clone / fork / tinker, and contribute to this repo.
