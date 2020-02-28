#!/bin/bash

die()
{
	echo $1
	exit 1
}

if [ "$TRAVIS_PULL_REQUEST" != "false" ]; then
	echo "Travis should not deploy from pull requests"
	exit 0
fi

append_readme()
{
	for arg in $FILES; do
		echo \* [$arg]\(https://github.com/FWGS/xash3d-deploy/blob/$DEPLOY_BRANCH-$TRAVIS_BRANCH/$arg\?raw\=true\) >> README.md
		echo >> README.md
	done
}

generate_continious_tag()
{
	if [ $TRAVIS_BRANCH == "master" ]; then
		echo "continuous"
	else
		echo "continuous-$TRAVIS_BRANCH"
	fi
}

generate_readme()
{
	TAG=$(generate_continuous_tag)
#	echo \# Moved to GitHub Releases at [here]\(https://github.com/FWGS/xash3d-fwgs/releases/tag/$TAG\) >> README.md
#	echo >> README.md
#	echo >> README.md
#	echo >> README.md
#	echo >> README.md
	echo \# $TRAVIS_BRANCH branch autobuilds from $DEPLOY_BRANCH >> README.md
	echo >> README.md
	echo Short changelog: >> README.md
	echo \`\`\` >> README.md
	(cd $TRAVIS_BUILD_DIR; TZ=UTC git log --pretty=format:'%h %ad %s' --date iso-local -n 10 $REV_RANGE)| cut -d ' ' -f 1-3,5-100 >> README.md
	echo \`\`\` >> README.md
	echo >> README.md
	echo [Code on GitHub]\(https://github.com/FWGS/xash3d-fwgs/tree/$TRAVIS_COMMIT\) >> README.md
	echo >> README.md
	echo [Full changelog for this build]\(https://github.com/FWGS/xash3d-fwgs/commits/$TRAVIS_COMMIT\) >> README.md
	echo >> README.md
	append_readme
	echo $TRAVIS_COMMIT > commit.txt
}

yadisk_download()
{
	FOLDER_NAME=$DEPLOY_BRANCH-$TRAVIS_BRANCH
	WEBDAV_SRV=https://$YANDEX_DISK_USER:$YANDEX_DISK_TOKEN@webdav.yandex.ru

	for file in $*; do
		curl -L $WEBDAV_SRV/$FOLDER_NAME/$file -o $file
	done
}

PUSHED_COMMIT=$(curl --fail https://raw.githubusercontent.com/FWGS/xash3d-deploy/$DEPLOY_BRANCH-$TRAVIS_BRANCH/commit.txt)
echo "Pushed commit: $PUSHED_COMMIT"
if [ ! -z "$PUSHED_COMMIT" ]; then
	REV_RANGE="HEAD...$PUSHED_COMMIT"
else
	REV_RANGE="HEAD"
fi

git config --global user.name FWGS-deployer
git config --global user.email FWGS-deployer@users.noreply.github.com

FILES=$*

# Create new repo with new files
mkdir xash3d-deploy
cd xash3d-deploy
git init
git remote add travis-deploy-public https://FWGS-deployer:${GH_TOKEN}@github.com/FWGS/xash3d-deploy.git
git checkout -b $DEPLOY_BRANCH-$TRAVIS_BRANCH
yadisk_download $FILES
generate_readme
git add .
git commit -m "Latest travis deploy $TRAVIS_COMMIT"
git push -q --force travis-deploy-public $DEPLOY_BRANCH-$TRAVIS_BRANCH >/dev/null 2>/dev/null

exit 0
