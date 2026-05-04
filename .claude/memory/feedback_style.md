---
name: Collaboration Style Feedback
description: How the user prefers to work and receive output from Claude
type: feedback
originSessionId: 2336d293-7b97-4ecb-9509-7252b79993a1
---
Provide specific file:line references in all code feedback — user navigates directly to them.

Do NOT run unnecessary verification/status commands (git diff --stat, git status, grep checks after edits). Trust the edits and move on. Every redundant tool call costs tokens.

**Why:** User explicitly said "do not waste my tokens" when I ran a diff after already completing all edits.

**How to apply:** When edits are done, they're done. Only run verification commands when something might have gone wrong (compile error, complex sed, etc.). Skip routine "let me check the result" calls.
