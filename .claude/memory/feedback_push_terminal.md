---
name: Git push requires user terminal
description: git push over HTTPS fails non-interactively — user must run it themselves
type: feedback
originSessionId: c4148f1c-fc4f-436a-8b45-e1f432add3ec
---
Never attempt `git push` expecting it to succeed in the non-interactive tool environment when the remote is HTTPS.

**Why:** The credential prompt (`/dev/tty`) is unavailable in the sandboxed shell, so git exits with code 128. This has happened repeatedly.

**How to apply:** Commit normally, then tell the user to run `git push` (or `git push -u origin <branch>`) from their own terminal. One-liner is enough — no need to explain why every time.
