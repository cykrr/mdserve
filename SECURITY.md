# Security Policy

## Reporting a vulnerability

If you discover a security vulnerability in mdserve, please email
**krrm@krr.cl** rather than opening a public issue.

I'll respond within 72 hours with an assessment and expected timeline
for a fix.

## Scope

Security issues in scope include (but are not limited to):

- Path traversal / sandbox escape from the served root directory
- Server-Side Request Forgery (SSRF) via `/remote/`
- Cross-Site Scripting (XSS) in rendered pages or directory listings
- Denial of Service (resource exhaustion, crashes)
- Remote Code Execution

## Hardening

mdserve is compiled with all practical compiler-level hardening:

- `-D_FORTIFY_SOURCE=2` — glibc buffer overflow detection
- `-fstack-protector-strong` — stack canaries
- `-fPIE -pie` — position-independent executable (ASLR)
- `-Wl,-z,relro -Wl,-z,now` — full RELRO (read-only GOT)

A separate sanitizer build is available for fuzzing and testing:

```sh
make hardened    # -fsanitize=address,undefined + -Werror
make test        # regression suite (16 tests)
```

CI on every push runs both.
