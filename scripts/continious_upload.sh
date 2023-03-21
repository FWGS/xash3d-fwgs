#!/bin/bash

# MIT License
#
# Copyright (c) 2016 Simon Peter
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

set +x # Do not leak information

# Exit immediately if one of the files given as arguments is not there
# because we don't want to delete the existing release if we don't have
# the new files that should be uploaded
for file in "$@"
do
    if [ ! -e "$file" ]
    then echo "$file is missing, giving up." >&2; exit 1
    fi
done

if [ $# -eq 0 ]; then
    echo "No artifacts to use for release, giving up."
    exit 0
fi

if command -v sha256sum >/dev/null 2>&1 ; then
  shatool="sha256sum"
elif command -v shasum >/dev/null 2>&1 ; then
  shatool="shasum -a 256" # macOS fallback
else
  echo "Neither sha256sum nor shasum is available, cannot check hashes"
fi

RELEASE_BODY=""
GIT_REPO_SLUG="$REPO_SLUG"

if [ ! -z "$GITHUB_ACTIONS" ] ; then
  GIT_COMMIT="$GITHUB_SHA"
  GIT_REPO_SLUG="$GITHUB_REPOSITORY"
  if [[ "$GITHUB_REF" == "refs/tags/"* ]] ; then
    GIT_TAG="${GITHUB_REF#refs/tags/}"
  fi
  RELEASE_BODY="GitHub Actions build log: $GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID"
fi

if [ ! -z "$UPLOADTOOL_BODY" ] ; then
  RELEASE_BODY="$UPLOADTOOL_BODY"
fi

# The calling script (usually .travis.yml) can set a suffix to be used for
# the tag and release name. This way it is possible to have a release for
# the output of the CI/CD pipeline (marked as 'continuous') and also test
# builds for other branches.
# If this build was triggered by a tag, call the result a Release
if [ ! -z "$UPLOADTOOL_SUFFIX" ] ; then
  if [ "$UPLOADTOOL_SUFFIX" = "$GIT_TAG" ] ; then
    RELEASE_NAME="$GIT_TAG"
    RELEASE_TITLE="Release build ($GIT_TAG)"
    is_prerelease="false"
  else
    RELEASE_NAME="continuous-$UPLOADTOOL_SUFFIX"
    RELEASE_TITLE="Continuous build ($UPLOADTOOL_SUFFIX)"
    if [ -z "$UPLOADTOOL_ISPRERELEASE" ] ; then
      is_prerelease="false"
    else
      is_prerelease="true"
    fi

  fi
else
  if [ "$GITHUB_ACTIONS" = "true" ]; then
    if [ "$GITHUB_REF_TYPE" == "branch" ]; then
      if [ "$GITHUB_REF_NAME" == "master" ]; then
        RELEASE_NAME="continuous"
        RELEASE_TITLE="Continuous build"
      else
        RELEASE_NAME="continuous-$GITHUB_REF_NAME"
        RELEASE_TITLE="Continuous build ($GITHUB_REF_NAME)"
      fi
      if [ -z "$UPLOADTOOL_ISPRERELEASE" ]; then
        is_prerelease="false"
      else
        is_prerelease="true"
      fi
    elif [ "$GITHUB_REF_TYPE" == "tag" ]; then
      case $(tr '[:upper:]' '[:lower:]' <<< "$GITHUB_REF_NAME") in
        *-alpha*|*-beta*|*-rc*)
          RELEASE_NAME="$GITHUB_REF_NAME"
          RELEASE_TITLE="Pre-release build ($GITHUB_REF_NAME)"
          is_prerelease="true"
          ;;
        *)
          RELEASE_NAME="$GITHUB_REF_NAME"
          RELEASE_TITLE="Release build ($GITHUB_REF_NAME)"
          is_prerelease="false"
          ;;
      esac
    fi
  else
    # ,, is a bash-ism to convert variable to lower case
    case $(tr '[:upper:]' '[:lower:]' <<< "$GIT_TAG") in
      "")
        # Do not use "latest" as it is reserved by GitHub
        RELEASE_NAME="continuous"
        RELEASE_TITLE="Continuous build"
        if [ -z "$UPLOADTOOL_ISPRERELEASE" ] ; then
          is_prerelease="false"
        else
          is_prerelease="true"
        fi
        ;;
      *-alpha*|*-beta*|*-rc*)
        RELEASE_NAME="$GIT_TAG"
        RELEASE_TITLE="Pre-release build ($GIT_TAG)"
        is_prerelease="true"
        ;;
      *)
        RELEASE_NAME="$GIT_TAG"
        RELEASE_TITLE="Release build ($GIT_TAG)"
        is_prerelease="false"
        ;;
    esac
  fi
fi

# Do not upload non-master branch builds
if [ "$GITHUB_EVENT_NAME" == "pull_request" ] ; then
  echo "Release uploading disabled for pull requests, uploading to transfer.sh instead"
  rm -f ./uploaded-to
  for FILE in "$@" ; do
    BASENAME="$(basename "${FILE}")"
    curl --upload-file $FILE "https://transfer.sh/$BASENAME" > ./one-upload
    echo "$(cat ./one-upload)" # this way we get a newline
    echo -n "$(cat ./one-upload)\\n" >> ./uploaded-to # this way we get a \n but no newline
  done
  $shatool "$@"
  exit 0
fi

if [ ! -z "$GITHUB_ACTIONS" ] ; then
  echo "Running on GitHub Actions"
  if [ -z "$GITHUB_TOKEN" ] ; then
    echo "\$GITHUB_TOKEN missing, please add the following to your run action:"
    echo "env:"
    echo "  GITHUB_TOKEN: \${{ secrets.GITHUB_TOKEN }}"
    exit 1
  fi
else
  echo "Not running on known CI"
  if [ -z "$GIT_REPO_SLUG" ] ; then
    read -r -p "Repo Slug (GitHub username/reponame): " GIT_REPO_SLUG
  fi
  if [ -z "$GITHUB_TOKEN" ] ; then
    read -r -s -p "Token (https://github.com/settings/tokens): " GITHUB_TOKEN
  fi
fi

tag_url="https://api.github.com/repos/$GIT_REPO_SLUG/git/refs/tags/$RELEASE_NAME"
tag_infos=$(curl -XGET --header "Authorization: token ${GITHUB_TOKEN}" "${tag_url}")
echo "tag_infos: $tag_infos"
tag_sha=$(echo "$tag_infos" | grep '"sha":' | head -n 1 | cut -d '"' -f 4 | cut -d '{' -f 1)
echo "tag_sha: $tag_sha"

release_url="https://api.github.com/repos/$GIT_REPO_SLUG/releases/tags/$RELEASE_NAME"
echo "Getting the release ID..."
echo "release_url: $release_url"
release_infos=$(curl -XGET --header "Authorization: token ${GITHUB_TOKEN}" "${release_url}")
echo "release_infos: $release_infos"
release_id=$(echo "$release_infos" | grep "\"id\":" | head -n 1 | tr -s " " | cut -f 3 -d" " | cut -f 1 -d ",")
echo "release ID: $release_id"
upload_url=$(echo "$release_infos" | grep '"upload_url":' | head -n 1 | cut -d '"' -f 4 | cut -d '{' -f 1)
echo "upload_url: $upload_url"
release_url=$(echo "$release_infos" | grep '"url":' | head -n 1 | cut -d '"' -f 4 | cut -d '{' -f 1)
echo "release_url: $release_url"
target_commit_sha=$(echo "$release_infos" | grep '"target_commitish":' | head -n 1 | cut -d '"' -f 4 | cut -d '{' -f 1)
echo "target_commit_sha: $target_commit_sha"

if [ "$GIT_COMMIT" != "$target_commit_sha" ] ; then

  echo "GIT_COMMIT != target_commit_sha, hence deleting $RELEASE_NAME..."

  if [ ! -z "$release_id" ]; then
    delete_url="https://api.github.com/repos/$GIT_REPO_SLUG/releases/$release_id"
    echo "Delete the release..."
    echo "delete_url: $delete_url"
    curl -XDELETE \
        --header "Authorization: token ${GITHUB_TOKEN}" \
        "${delete_url}"
  fi

  # echo "Checking if release with the same name is still there..."
  # echo "release_url: $release_url"
  # curl -XGET --header "Authorization: token ${GITHUB_TOKEN}" \
  #     "$release_url"

  if [ "$RELEASE_NAME" == "continuous" ] ; then
    # if this is a continuous build tag, then delete the old tag
    # in preparation for the new release
    echo "Delete the tag..."
    delete_url="https://api.github.com/repos/$GIT_REPO_SLUG/git/refs/tags/$RELEASE_NAME"
    echo "delete_url: $delete_url"
    curl -XDELETE \
        --header "Authorization: token ${GITHUB_TOKEN}" \
        "${delete_url}"
  fi

  echo "Create release..."

  release_infos=$(curl -H "Authorization: token ${GITHUB_TOKEN}" \
       --data '{"tag_name": "'"$RELEASE_NAME"'","target_commitish": "'"$GIT_COMMIT"'","name": "'"$RELEASE_TITLE"'","body": "'"$RELEASE_BODY"'","draft": false,"prerelease": '$is_prerelease'}' "https://api.github.com/repos/$GIT_REPO_SLUG/releases")

  echo "$release_infos"

  unset upload_url
  upload_url=$(echo "$release_infos" | grep '"upload_url":' | head -n 1 | cut -d '"' -f 4 | cut -d '{' -f 1)
  echo "upload_url: $upload_url"

  unset release_url
  release_url=$(echo "$release_infos" | grep '"url":' | head -n 1 | cut -d '"' -f 4 | cut -d '{' -f 1)
  echo "release_url: $release_url"

fi # if [ "$GIT_COMMIT" != "$tag_sha" ]

if [ -z "$release_url" ] ; then
	echo "Cannot figure out the release URL for $RELEASE_NAME"
	exit 1
fi

echo "Upload binaries to the release..."

# Need to URL encode the basename, so we have this function to do so
urlencode() {
  # urlencode <string>
  old_lc_collate=$LC_COLLATE
  LC_COLLATE=C
  local length="${#1}"
  for (( i = 0; i < length; i++ )); do
    local c="${1:$i:1}"
    case $c in
      [a-zA-Z0-9.~_-]) printf '%s' "$c" ;;
      *) printf '%%%02X' "'$c" ;;
    esac
  done
  LC_COLLATE=$old_lc_collate
}

for FILE in "$@" ; do
  FULLNAME="${FILE}"
  BASENAME="$(basename "${FILE}")"

  for retries in {1..10}; do
    echo "Upload attempt $retries"

    if curl -H "Authorization: token ${GITHUB_TOKEN}" \
         -H "Accept: application/vnd.github.manifold-preview" \
         -H "Content-Type: application/octet-stream" \
         --data-binary "@$FULLNAME" \
         "$upload_url?name=$(urlencode "$BASENAME")"; then
      break
    fi

    sleep 1m # try to avoid ratelimits???
    echo ""
  done
done

$shatool "$@"

if [ "$GIT_COMMIT" != "$tag_sha" ] ; then
  echo "Publish the release..."

  release_infos=$(curl -H "Authorization: token ${GITHUB_TOKEN}" \
       --data '{"draft": false}' "$release_url")

  echo "$release_infos"
fi # if [ "$GIT_COMMIT" != "$tag_sha" ]
