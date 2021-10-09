#!/bin/bash

GIT_REV_XML=$1/res/values/git-rev.xml

mkdir -p $(dirname $GIT_REV_XML)
git update-index --assume-unchanged $GIT_REV_XML

echo '<?xml version="1.0" encoding="utf-8"?>' > $GIT_REV_XML
echo '<resources>' >> $GIT_REV_XML
echo -n '<string name="git_revisions">' >> $GIT_REV_XML
echo -n '<b>Version information:</b>\n' >> $GIT_REV_XML
git submodule --quiet foreach --recursive \
	'echo -n \<b\>`basename $name`:\</b\>\ \
`git log --abbrev-commit --pretty=oneline -1 | sed "s/\&/\&amp;/g;s/>/\&gt;/g;s/</\&lt;/g"`\\\\n' \
| sed -e "s|'|\\\'|g" >> $GIT_REV_XML
echo -n $USER@$(hostname) $(date +%H:%M:%S-%d-%m-%y) >> $GIT_REV_XML
echo '</string>' >> $GIT_REV_XML
echo '</resources>' >> $GIT_REV_XML
cat $GIT_REV_XML
