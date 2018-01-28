#!/bin/bash
if [ "$TRAVIS_PULL_REQUEST" != "false" ]; then
 echo "Travis should not deploy from pull requests"
 exit 0
else
 SOURCE_NAME=$1
 shift
 mkdir xash3d-travis
 cp -a $* xash3d-travis/
 cd xash3d-travis
 git init
 git config user.name FWGS-deployer
 git config user.email FWGS-deployer@users.noreply.github.com
 git remote add travis-deploy-public https://FWGS-deployer:${GH_TOKEN}@github.com/FWGS/xash3d-deploy.git
 echo \# $TRAVIS_BRANCH branch autobuilds from $SOURCE_NAME >> README.md
 echo >> README.md
 echo Short changelog: >> README.md
 echo \`\`\` >> README.md
 (cd $TRAVIS_BUILD_DIR;git log --pretty=format:'%h %ad %s' --date iso -n 10 HEAD `curl https://raw.githubusercontent.com/FWGS/xash3d-deploy/$SOURCE_NAME-$TRAVIS_BRANCH/commit.txt`
.. )| cut -d ' ' -f 1-3,5-100 >> README.md
 echo \`\`\` >> README.md
 echo >> README.md
 echo [Code on GitHub]\(https://github.com/FWGS/xash3d/tree/$TRAVIS_COMMIT\) >> README.md
 echo >> README.md
 echo [Full changelog for this build]\(https://github.com/FWGS/xash3d/commits/$TRAVIS_COMMIT\) >> README.md
 echo >> README.md
 for arg in $*; do
  echo \* [$arg]\(https://github.com/FWGS/xash3d-deploy/blob/$SOURCE_NAME-$TRAVIS_BRANCH/$arg\?raw\=true\) >> README.md
  echo >> README.md
 done
 echo $TRAVIS_COMMIT > commit.txt
 git add .
 git commit -m "Latest travis deploy $TRAVIS_COMMIT"
 git checkout -b $SOURCE_NAME-$TRAVIS_BRANCH
 git push -q --force travis-deploy-public $SOURCE_NAME-$TRAVIS_BRANCH >/dev/null 2>/dev/null
 git checkout -b $SOURCE_NAME-latest
 git push -q --force travis-deploy-public $SOURCE_NAME-latest >/dev/null 2>/dev/null
fi
exit 0
