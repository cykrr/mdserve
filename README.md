# mdserve

A minimal HTTP server that renders Markdown files as styled HTML pages. Written in C — lightweight, no dependencies beyond what's in the repo.

## Features

- **Markdown → HTML** — Uses [md4c](https://github.com/mity/md4c) with GitHub dialect (tables, strikethrough, task lists, autolinks).
- **Clean typography** — System font stack, responsive max-width layout, dark mode support via `prefers-color-scheme`.
- **KaTeX math** — Inline `$...$` and display `$$...$$` rendered via KaTeX auto-render.
- **Directory browsing** — Serves index.md for directories or lists contents if no index.md exists, with `..` navigation.
- **Path traversal protection** — URL decoding happens before path validation, preventing `%2e%2e` bypasses.
- **Proper HTTP status codes** — Real 404 (not 200 with a not-found page), 403 for traversal attempts, 301 redirects for missing trailing slashes.
- **Static file serving** — Non-markdown files are served by mongoose's built-in static handler (MIME detection, Range support).
- **No auth, no TLS** — Designed for local networks or behind a reverse proxy.

## Build

### Dependencies

- GCC (or any C99 compiler)
- [md4c](https://github.com/mity/md4c) — the Markdown parser library (`libmd4c` + `libmd4c-html`)

On Debian/Ubuntu:

```sh
sudo apt install libmd4c-dev
```

### Compile

```sh
make
```

This produces a statically-linked binary `mdserve`.

## Usage

```sh
./mdserve [listen-url] [root-dir]
```

- `listen-url` — HTTP URL to listen on (default: `http://0.0.0.0:8080`).
- `root-dir` — Directory to serve (default: `./md`).

### Examples

```sh
# Serve ./md on port 8080
./mdserve

# Serve ./notes on port 8080
./mdserve http://0.0.0.0:8080 ./notes

# Bind to a specific interface (recommended for security)
./mdserve http://100.64.0.12:8080 /home/user/notes
```

## Project structure

```
.
├── main.c          # HTTP server, request routing, directory listing
├── src/
│   ├── md.c        # Markdown → HTML conversion (md4c wrapper)
│   └── membuf.c    # Growable memory buffer (from md4c examples)
├── include/
│   ├── md.h
│   └── membuf.h
├── head.html       # HTML <head> with styles, KaTeX, open <body>
├── tail.html       # Closing </body></html>
├── 404.html        # 404 page content (injected between head/tail)
├── mongoose.c      # Vendored mongoose 7.22 (MIT license)
├── mongoose.h
├── Makefile
└── md/             # Default root for Markdown files
    └── index.md    # Example page
```

## Customisation

- **head.html** — Contains the full `<head>` with CSS and KaTeX loading, plus the opening `<body>` tag. Edit this to change styles, fonts, or add analytics.
- **tail.html** — Just `</body></html>`.
- **404.html** — Content for the 404 error page.
- All three template files are read from the current working directory, not from the served root.

## License

GNU General Public License v2. See [LICENSE](LICENSE).

Contains vendored [mongoose](https://github.com/cesanta/mongoose) 7.22 (MIT license).