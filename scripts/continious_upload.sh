##################################
#
#        GitHub Releases
#
##################################

set +x

wget -O upload.sh "https://raw.githubusercontent.com/FWGS/uploadtool/gha-fixes/upload.sh"
chmod +x upload.sh

./upload.sh $*

