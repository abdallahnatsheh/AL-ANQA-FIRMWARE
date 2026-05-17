---
name: Collaboration Rules
description: All feedback rules for how to work with this user
type: feedback
---

1. **No AskUserQuestion tool** — pick the best approach, propose it in plain text, let user redirect.
2. **No git push** — HTTPS credential prompt blocks in sandbox. Tell user to run `git push` themselves.
3. **No full license text via Write** — AGPL-3.0 full text triggers content filter. User pastes manually.
4. **Reuse existing code** — read the class before writing new functions. Call what exists; don't reimplement.
5. **No redundant verification** — don't run diff/status/grep after edits just to confirm. Trust the edit and move on. User said "do not waste my tokens".
6. **Verify APIs before using them** — check actual header files for method names before writing code (e.g. `getInitialized()` not `isInitialized()`). Wrong API names cause compile errors that waste tokens.
7. **Test logic mentally before writing** — trace through crash scenarios (uninitialized deinit, duplicate service on second run) before submitting. Don't iterate fixes in the chat.
