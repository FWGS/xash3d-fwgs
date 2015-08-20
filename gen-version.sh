git update-index --assume-unchanged res/values/git-rev.xml
echo '<?xml version="1.0" encoding="utf-8"?>' > res/values/git-rev.xml
echo '<resources>' >> res/values/git-rev.xml
echo '<string name="git_revisions">' >> res/values/git-rev.xml
echo '<b>Version information:</b>' $* \\n >> res/values/git-rev.xml
echo '<b>android:</b>' $(git log --abbrev-commit --pretty=oneline -1) _endl \
'<b>engine:</b>' $(cd jni/src/Xash3D/*/;git log --abbrev-commit --pretty=oneline -1) '\n'\
'<b>client:</b>' $(cd jni/src/XashXT/*/;git log --abbrev-commit --pretty=oneline -1) ' \\n'\
'<b>halflife:</b>' $(cd jni/src/HLSDK/*/;git log --abbrev-commit --pretty=oneline -1) ' \\\n'\
'<b>SDL2:</b>' $(cd jni/src/SDL2/*/;git log --abbrev-commit --pretty=oneline -1) \n \
'<b>TouchControls:</b>' $(cd jni/src/MobileTouchControls/*/;git log --abbrev-commit --pretty=oneline -1) \\\n \
'<b>nanogl:</b>' $(cd jni/src/NanoGL/*/;git log --abbrev-commit --pretty=oneline -1) \\\\n \
 | sed -e s/\'/\\\\\'/g -e s/_endl/'\\n'/ >> res/values/git-rev.xml
echo '</string>' >> res/values/git-rev.xml
echo '</resources>' >> res/values/git-rev.xml
cat res/values/git-rev.xml
