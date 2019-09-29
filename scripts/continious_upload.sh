##################################
#
#        GitHub Releases
#
##################################

set +x

# Disabled until GitHub sends prereleases to email

# wget -O upload.sh "https://raw.githubusercontent.com/FWGS/uploadtool/master/upload.sh"
# chmod +x upload.sh

# export GITHUB_TOKEN=$GH_TOKEN
# ./upload.sh $*

##################################
#
#        Yandex.Disk
#
##################################

FOLDER_NAME=$DEPLOY_BRANCH-$TRAVIS_BRANCH
WEBDAV_SRV=https://$YANDEX_DISK_USER:$YANDEX_DISK_TOKEN@webdav.yandex.ru

for file in $*; do
	echo "Uploading $file..."
	curl -T $file $WEBDAV_SRV/$FOLDER_NAME/$file
done
