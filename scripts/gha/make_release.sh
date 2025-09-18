#!/bin/bash

if ! command -v gh > /dev/null; then
	echo "Install GitHub CLI tool"
	exit 1
fi

RELEASE_NAME=$1
BRANCH_NAME=$2
DEFAULT_BRANCH_NAME=master

if [ -z "$RELEASE_NAME" ] || [ -z "$BRANCH_NAME" ]; then
	echo "Invalid arguments"
	exit 1
fi

if [ "$BRANCH_NAME" == "$DEFAULT_BRANCH_NAME" ]; then
	RELEASE_TAG="continuous"
else
	RELEASE_TAG="continuous-$BRANCH_NAME"
fi

gh release delete "$RELEASE_TAG" \
	--yes \
	--cleanup-tag \
	--repo "$GITHUB_REPOSITORY" || true

gh run download "$GITHUB_RUN_ID" \
	--dir artifacts/ \
	--repo "$GITHUB_REPOSITORY"

pushd artifacts/ || exit 1
echo "Found artifacts:"
ls
for i in $(find -mindepth 1 -maxdepth 1 -type d); do
	mv "$i"/* .
	rm -rf "$i"
done
echo "Repackaged artifacts:"
ls -R
popd || exit 1

echo "Generated notes:"
rm release-notes && touch release-notes
if [ -n "$BASE_BUILDNUM_DATE" ]; then
	LAST_COMMIT_DATE=$(git log -1 --format=%ci)
	BUILDNUM=$(( ( $(date -d "$LAST_COMMIT_DATE" +%s ) - $(date -d "$BASE_BUILDNUM_DATE" +%s) ) / 86400 ))
	echo "buildnum $BUILDNUM" | tee -a release-notes
fi

sleep 20s
gh release create "$RELEASE_TAG" artifacts/* \
	--title "$RELEASE_NAME Continuous $BRANCH_NAME Build" \
	--notes-file release-notes \
	--target "$GITHUB_SHA" \
	--repo "$GITHUB_REPOSITORY" \
	--prerelease

rm release-notes
