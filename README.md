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

## Project structure

```
.
├── main.c          # HTTP server, routing, directory listing, remote fetch
├── src/
│   ├── md.c        # Markdown → HTML (md4c wrapper)
│   └── membuf.c    # Growable memory buffer
├── include/
│   ├── md.h
│   └── membuf.h
├── head.html       # HTML <head> with styles, KaTeX, open <body>
├── tail.html       # Closing </body></html>
├── 404.html        # 404 page content
├── mongoose.c      # Vendored mongoose 7.22 (MIT)
├── mongoose.h
├── Makefile
├── deploy.sh       # git-archive deploy to remote server
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
