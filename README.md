# mdserve

> A tiny Markdown server for people who keep their notes in Git.

`mdserve` lets you browse a directory of Markdown files over HTTP. No database. No Electron. No proprietary format. Just files.

## Why?

Most note-taking software wants to own your notes.

mdserve is the opposite:

- Markdown files on disk
- Versioned with Git
- Editable with any editor
- Accessible from a browser
- Small enough to understand in an afternoon

## Features

- **Markdown → HTML** — Uses [md4c](https://github.com/mity/md4c) with GitHub dialect (tables, strikethrough, task lists, autolinks)
- **Clean typography** — System font stack, responsive max-width layout, dark mode via `prefers-color-scheme`
- **KaTeX math** — Inline `$...$` and display `$$...$$` rendered via KaTeX auto-render
- **Directory browsing** — Serves `index.md` for directories or lists contents with `..` navigation
- **Remote fetch** — `/remote/<url-encoded-url>` fetches and renders remote Markdown (e.g. from GitHub)
- **Path traversal protection** — URL decoding happens before path validation, preventing `%2e%2e` bypasses
- **Proper HTTP status codes** — Real 404, 403 for traversal attempts, 301 redirects for missing trailing slashes
- **Static file serving** — Non-markdown files served with MIME detection and Range support
- **No auth, no TLS** — Designed for local networks or behind a reverse proxy

## Philosophy

mdserve is not trying to replace your editor. It's a thin HTTP layer over your Markdown repository — if your notes already live in Git, mdserve lets you access them anywhere without introducing another database, sync service, or proprietary workspace.

## Build

### Dependencies

- GCC (or any C99 compiler)
- [md4c](https://github.com/mity/md4c) — Markdown parser (`libmd4c` + `libmd4c-html`)
- OpenSSL (for HTTPS remote fetch)

On Debian/Ubuntu:

```sh
sudo apt install libmd4c-dev libssl-dev
```

### Compile

```sh
make
```

## Usage

```sh
./mdserve [listen-url] [root-dir]
```

- `listen-url` — HTTP URL to listen on (default: `http://0.0.0.0:8080`)
- `root-dir` — Directory to serve (default: `./md`)

### Examples

```sh
# Serve ./md on port 8080
./mdserve

# Serve ./notes on port 8080
./mdserve http://0.0.0.0:8080 ./notes

# Bind to a specific interface
./mdserve http://100.64.0.12:8080 /home/user/notes
```

### Remote Markdown

```
/remote/https%3A%2F%2Fraw.githubusercontent.com%2Fuser%2Frepo%2Fmain%2FREADME.md
```

Only `http://` and `https://` URLs ending in `.md` are allowed. Raw IP addresses and private/reserved IP ranges are blocked.

## Static site generation

`mdbuild` renders the same tree to plain HTML for hosting where you can't run a daemon (shared hosting, object storage, GitHub Pages). It shares the render core with `mdserve` but links neither mongoose nor OpenSSL.

```sh
make mdbuild
./mdbuild [src-dir] [out-dir]     # defaults: ./md  ./site
make site                         # same thing
```

### Publishing is opt-in

A `.md` file is only emitted if its YAML frontmatter says so:

```markdown
---
publish: true
title: Optional title for directory listings
---

# Your note
```

Files without the flag are skipped. `mdserve` ignores the flag entirely — browsing locally shows everything — but both binaries strip the frontmatter block before rendering, so it never leaks into the output.

Non-Markdown files (images, attachments) are **copied unconditionally**, since a published note that links to an image needs that image to exist. Don't put anything in the tree you wouldn't publish as a raw file.

### URL layout

Output uses directory-style URLs: `md/notes/rust.md` → `site/notes/rust/index.html`, served as `/notes/rust/`. Because that puts every page one directory deeper than its source, relative `.md` links are rewritten to **root-absolute** paths (`other.md` → `/notes/other/`). The generated site therefore has to live at the root of its domain, not under a subpath.

`index.md` becomes the directory's page. Directories without a published `index.md` get a generated listing using the same markup as the server's.

A `.htaccess` (Apache `ErrorDocument`, `Options -Indexes`) and a rendered `404.html` are written to the output root.

### Deploying

```sh
./deploy-static.sh            # build + rsync to floreria:domains/notes.krr.cl/public_html
./deploy-static.sh --dry-run  # show what would change
```

Override `REMOTE`, `DOMAIN`, `SRC`, `OUT` via environment. The sync uses `--delete`, so the remote docroot is owned entirely by the script — unpublishing a note removes it from the server, and hand-edited remote files get wiped.

## Project structure

```
.
├── main.c          # HTTP server, routing, directory listing, remote fetch
├── mdbuild.c       # Static site generator (no mongoose, no TLS)
├── src/
│   ├── md.c        # Markdown → HTML (md4c wrapper)
│   ├── frontmatter.c  # YAML frontmatter scanner (publish flag, title)
│   └── membuf.c    # Growable memory buffer
├── include/
│   ├── md.h
│   ├── frontmatter.h
│   └── membuf.h
├── head.html       # HTML <head> with styles, KaTeX, open <body>
├── tail.html       # Closing </body></html>
├── 404.html        # 404 page content
├── mongoose.c      # Vendored mongoose 7.22 (MIT)
├── mongoose.h
├── Makefile
├── deploy.sh       # git-archive deploy of the server to gcp1
├── deploy-static.sh   # build + rsync the static site to shared hosting
└── md/             # Default root for Markdown files
    └── index.md
```

## Customisation

- **head.html** — Full `<head>` with CSS and KaTeX, plus opening `<body>`. Edit to change styles, fonts, or add analytics.
- **tail.html** — Just `</body></html>`.
- **404.html** — Content for the 404 error page.
- All three templates are read from the working directory, not the served root.

## Why not Obsidian / Notion?

Because plain files, Git history, no vendor lock-in, no Electron, no cloud account, complete control over your data.

## License

GNU General Public License v2. See [LICENSE](LICENSE).

Contains vendored [mongoose](https://github.com/cesanta/mongoose) 7.22 (MIT license).
