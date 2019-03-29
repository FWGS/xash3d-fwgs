#!/bin/bash

die()
{
	echo $1
	exit 1
}

if [ "$TRAVIS_PULL_REQUEST" != "false" ]; then
	die "Travis should not deploy from pull requests"
fi

commit_files()
{
	git add .
	git commit -m "Latest travis deploy $TRAVIS_COMMIT"
}

force_push()
{
	git push -q --force travis-deploy-public $SOURCE_NAME-$TRAVIS_BRANCH >/dev/null 2>/dev/null
}

append_readme()
{
	for arg in $FILES; do
		echo \* [$arg]\(https://github.com/FWGS/xash3d-deploy/blob/$SOURCE_NAME-$TRAVIS_BRANCH/$arg\?raw\=true\) >> README.md
		echo >> README.md
	done
}

download_repo_and_copy_files()
{
	git clone https://github.com/FWGS/xash3d-deploy -b $SOURCE_NAME-$TRAVIS_BRANCH --depth=1
	cp -a $FILES xash3d-deploy/
	cd xash3d-deploy
	git remote add travis-deploy-public https://FWGS-deployer:${GH_TOKEN}@github.com/FWGS/xash3d-deploy.git
}

init_repo_and_copy_files()
{
	mkdir xash3d-deploy
	cp -a $FILES xash3d-deploy
	cd xash3d-deploy
	git init
	git remote add travis-deploy-public https://FWGS-deployer:${GH_TOKEN}@github.com/FWGS/xash3d-deploy.git
	git checkout -b $SOURCE_NAME-$TRAVIS_BRANCH
}

push_until_success()
{
	git push travis-deploy-public $SOURCE_NAME-$TRAVIS_BRANCH

	# probably will never occur, just in case
	count=0
	while [ $? -ne 0 ] && [ $count -lt 5 ]
	do
		((count++))
		sleep 20s
		git pull travis-deploy-public $SOURCE_NAME-$TRAVIS_BRANCH -X theirs || die "Can't pull from repository!"
		append_readme # Re-add lost readme lost during merge
		commit_files
		git push travis-deploy-public $SOURCE_NAME-$TRAVIS_BRANCH
	done
}

generate_readme()
{
	echo \# $TRAVIS_BRANCH branch autobuilds from $SOURCE_NAME >> README.md
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

SOURCE_NAME=$1
shift

PUSHED_COMMIT=$(curl --fail https://raw.githubusercontent.com/FWGS/xash3d-deploy/$SOURCE_NAME-$TRAVIS_BRANCH/commit.txt)
echo "Pushed commit: $PUSHED_COMMIT"
if [ ! -z "$PUSHED_COMMIT" ]; then
	REV_RANGE="HEAD...$PUSHED_COMMIT"
else
	REV_RANGE="HEAD"
fi

git config --global user.name FWGS-deployer
git config --global user.email FWGS-deployer@users.noreply.github.com

FILES=$*

if [ "$TRAVIS_COMMIT" != "$PUSHED_COMMIT" ]; then
	# Create new repo with new files
	init_repo_and_copy_files
	generate_readme
	commit_files
	force_push
else
	# download repo and commit new files
	download_repo_and_copy_files
	append_readme
	commit_files
	push_until_success
fi

exit 0
