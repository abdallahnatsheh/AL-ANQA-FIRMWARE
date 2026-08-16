# Git hooks

Repo-tracked hooks that keep sensitive data out of history.

## Enable (one time, per clone)

```bash
git config core.hooksPath .githooks
```

## `pre-commit`

Runs before every commit and:

- **Blocks** staging any packet capture (`*.cap` / `*.pcap` / `*.pcapng`) — these always
  contain real SSIDs / MACs / handshakes and must never enter the repo.
- **Blocks** a MAC-address-looking string (`XX:XX:XX:XX:XX:XX`) added to a doc/data file
  (`*.md` / `*.txt` / `*.csv`). Code files are exempt — OUI tables and test frames
  legitimately contain MACs.

Override a genuine false positive with `git commit --no-verify`.

Always review `git diff --cached` for real network names and cracked passwords before
committing — the hook is a backstop, not a substitute.
