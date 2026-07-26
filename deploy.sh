#!/bin/bash
# deploy.sh — git-archive snapshot → gcp1 → build → replace binary → restart.
set -euo pipefail

REMOTE="${1:-gcp1}"
BRANCH="$(git branch --show-current)"
TARBALL="/tmp/mdserve-${BRANCH}.tar.gz"

echo "==> creating archive from branch '${BRANCH}'"
git archive --format=tar.gz -o "$TARBALL" HEAD

echo "==> copying to ${REMOTE}:/tmp/"
scp "$TARBALL" "${REMOTE}:/tmp/"

echo "==> building and installing on ${REMOTE}"
ssh "$REMOTE" bash -s <<'ENDSCRIPT'
set -euo pipefail

TARBALL=$(ls -t /tmp/mdserve-*.tar.gz 2>/dev/null | head -1)
if [ -z "$TARBALL" ]; then
  echo "ERROR: no tarball found"
  exit 1
fi

WORKDIR=$(mktemp -d /tmp/mdserve-build.XXXXXX)
trap 'rm -rf "$WORKDIR"' EXIT

echo "--> extracting $(basename "$TARBALL")"
tar xzf "$TARBALL" -C "$WORKDIR"
cd "$WORKDIR"

echo "--> building"
make

echo "--> stopping mdserve"
sudo systemctl stop mdserve

echo "--> installing binary"
install -m 755 mdserve /home/magickaito2141/mdserve/mdserve

echo "--> installing templates"
install -m 644 head.html tail.html 404.html /home/magickaito2141/mdserve/

echo "--> starting mdserve"
sudo systemctl start mdserve

echo "--> status"
sudo systemctl status mdserve --no-pager || true
ENDSCRIPT

rm -f "$TARBALL"
echo "==> done"
