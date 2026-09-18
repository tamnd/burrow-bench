#!/bin/sh
# Make sure there is a burrow checkout at BURROW_DIR.
#
# Three cases, and the important one is the second. If BURROW_DIR is somewhere
# the caller chose, it is their working copy and we do not touch it, because
# the whole point of BURROW_DIR=../burrow is to measure uncommitted work. Only
# the default location under build/ gets cloned and updated.
#
# Copyright 2026 The burrow Authors. All rights reserved.
# Use of this source code is governed by a BSD-style licence that can be found
# in the LICENSE file.

set -eu

cd "$(dirname "$0")/.."

BUILD=${BUILD:-build}
BURROW_DIR=${BURROW_DIR:-$BUILD/burrow}
BURROW_REPO=${BURROW_REPO:-https://github.com/tamnd/burrow.git}
BURROW_REF=${BURROW_REF:-main}

# Somebody else's checkout. Leave it alone, but say which one, because a number
# measured against a tree nobody can name is not worth recording.
case "$BURROW_DIR" in
"$BUILD"/*) ;;
*)
	if [ ! -d "$BURROW_DIR" ]; then
		echo "burrow-bench: no burrow at $BURROW_DIR" >&2
		exit 1
	fi
	id=$(git -C "$BURROW_DIR" describe --always --dirty 2>/dev/null || echo unknown)
	echo "using burrow at $BURROW_DIR ($id)"
	exit 0
	;;
esac

if [ ! -d "$BURROW_DIR/.git" ]; then
	mkdir -p "$(dirname "$BURROW_DIR")"
	git clone --quiet "$BURROW_REPO" "$BURROW_DIR"
fi

git -C "$BURROW_DIR" fetch --quiet origin "$BURROW_REF"
git -C "$BURROW_DIR" checkout --quiet FETCH_HEAD

id=$(git -C "$BURROW_DIR" describe --always 2>/dev/null || echo unknown)
echo "using burrow $BURROW_REF ($id)"
