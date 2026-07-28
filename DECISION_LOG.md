# Decision Log

- (2026-07-26) Installed mongoose as the HTTP Server. Dropped `server.{c.h}`.
- (2026-07-28) Added `mdbuild`, a static site generator sharing the render core
  with `mdserve`. Separate binary rather than a `--build` flag on the server, so
  the audited HTTP path stays untouched and the generator links no mongoose/TLS.
- (2026-07-28) Pretty-dir URLs (`foo.md` → `/foo/`). Consequence: every page sits
  one directory below its source, so relative links are rewritten root-absolute.
  This pins the generated site to the root of its domain.
- (2026-07-28) Publishing is opt-in via `publish: true` frontmatter — fails
  closed, so a typo keeps a note private instead of leaking it. Non-markdown
  assets are copied unconditionally (otherwise published notes break).
- (2026-07-28) Static target is `floreria` (DirectAdmin shared hosting,
  `domains/notes.krr.cl/public_html`), synced with `rsync --delete`.

