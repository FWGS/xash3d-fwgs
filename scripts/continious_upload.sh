##################################
#
#        GitHub Releases
#
##################################

set +x

wget -O upload.sh "https://raw.githubusercontent.com/probonopd/uploadtool/master/upload.sh"
chmod +x upload.sh

./upload.sh $*

