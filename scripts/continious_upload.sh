##################################
#
#        GitHub Releases
#
##################################

set +x

curl "https://raw.githubusercontent.com/FWGS/uploadtool/gha-fixes/upload.sh" -o upload.sh
chmod +x upload.sh

bash ./upload.sh $*

