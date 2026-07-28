#!/bin/bash
# deploy-static.sh — build the static site and rsync it to shared hosting.
#
# Counterpart to deploy.sh: that one ships the *server* to gcp1, this one ships
# *rendered HTML* to floreria, which is DirectAdmin shared hosting and can't run
# a long-lived binary.
set -euo pipefail

REMOTE="${REMOTE:-floreria}"
DOMAIN="${DOMAIN:-notes.krr.cl}"
SRC="${SRC:-./md}"
OUT="${OUT:-./site}"
DEST="domains/${DOMAIN}/public_html"

RSYNC_EXTRA=()
DRY_LABEL=""
for arg in "$@"; do
  case "$arg" in
    # -v only on dry runs: the point of a dry run is seeing the file list.
    -n|--dry-run) RSYNC_EXTRA+=(--dry-run --verbose); DRY_LABEL=" (dry run)" ;;
    *) echo "usage: $0 [-n|--dry-run]" >&2; exit 2 ;;
  esac
done

echo "==> building mdbuild"
make mdbuild

echo "==> generating site from ${SRC}"
rm -rf "$OUT"
./mdbuild "$SRC" "$OUT"   # exits non-zero if nothing carries 'publish: true'

# Second guard, independent of mdbuild's exit code: 404.html is emitted
# unconditionally, so check for the actual landing page.
if [ ! -f "$OUT/index.html" ]; then
  echo "ERROR: no index.html generated — refusing to push an empty site" >&2
  exit 1
fi

# --delete removes remote files that no longer exist locally. That is the point
# (unpublishing a note must actually unpublish it) but it also means the remote
# public_html is owned entirely by this script — do not hand-edit files there.
#
# The --exclude entries are paths the *host* owns, not us: .well-known carries
# ACME challenges (deleting it breaks certificate renewal) and cgi-bin is created
# by DirectAdmin. Excluded paths are protected from --delete by default, which is
# exactly why --delete-excluded must not be added here.
echo "==> syncing to ${REMOTE}:${DEST}${DRY_LABEL}"
rsync -az --delete "${RSYNC_EXTRA[@]}" \
  --chmod=D755,F644 \
  --exclude '/.well-known/' \
  --exclude '/cgi-bin/' \
  "$OUT"/ "${REMOTE}:${DEST}/"

echo "==> done — https://${DOMAIN}/"
