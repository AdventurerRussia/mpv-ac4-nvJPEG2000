# Rules and Guidelines

## Release Notes & Changelogs
- All release notes, changelogs, GitHub release descriptions, and lists of changes MUST ALWAYS be written in English.
- While direct conversation with the user may be in Russian (or the user's language), all release documentation, `release_notes_*.md` files, and GitHub releases must strictly use English.

## Release Archive Packaging Rules
- The release zip archive attached to GitHub releases MUST follow the dynamic version naming convention:
  `MPV-AC4-nvJPEG2000-Windows-<version>-x64.zip` (e.g., `MPV-AC4-nvJPEG2000-Windows-1.4.0-x64.zip`, where `<version>` dynamically reflects the released version tag).
- Inside the ZIP archive, the top-level directory MUST NOT contain the version number; it must ALWAYS be statically named:
  `MPV-AC4-nvJPEG2000-Windows-x64/`
- All player distribution files must be placed inside this `MPV-AC4-nvJPEG2000-Windows-x64/` folder.
- The player distribution must be clean: strictly no user scripts (`.lua`), temporary files, or configuration files (`.conf`).
- Placeholders for user storage MUST be included as empty directories: `portable_config/`, `scripts/`, `script-opts/`, `shaders/`.
- Temporary build or decoder directories like `DECODIR` must never be included.
