##################################
#
#        GitHub Releases
#
##################################

set +x

# Disabled until GitHub sends prereleases to email

wget -O upload.sh "https://raw.githubusercontent.com/probonopd/uploadtool/master/upload.sh"
chmod +x upload.sh

export GITHUB_TOKEN=$GH_TOKEN
./upload.sh $*

