---
name: Commit message style
description: Git commits must stay general — no host-specific/sensitive data
type: feedback
---

# Keep git commit messages general (2026-07-27, user-requested)

Do NOT put sensitive or host-specific data in commit messages or committed files:
- SD card mount paths (e.g. `/run/media/<user>/<SERIAL>`)
- the developer's system username
- device serials / volume IDs
- any other machine-local environment detail

On-device firmware paths (e.g. `/apps/nes/roms`) are fine — they're part of the product.

**Why:** commits are pushed to a public GitHub repo (`abdallahnatsheh/AL-ANQA-FIRMWARE`);
host paths/usernames/serials would leak the dev machine and don't belong in history.

**How to apply:** write concise conventional-commit messages at the firmware/feature level.
Never paste local filesystem paths, mount points, or usernames into a subject/body. When
migrating files on the user's physical SD card, do the filesystem work but describe it in
the commit only via the on-device `/apps/...` layout, not the host mount path.
