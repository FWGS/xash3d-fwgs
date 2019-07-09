##################################
#
#        GitHub Releases
#
##################################

set +x

wget -O upload.sh "https://raw.githubusercontent.com/FWGS/uploadtool/master/upload.sh"
chmod +x upload.sh

export GITHUB_TOKEN=$GH_TOKEN
./upload.sh $*
