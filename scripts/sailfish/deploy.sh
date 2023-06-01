#!/bin/bash

PASSWORD="12345"
IP="192.168.1.10"

if [ "$1" == "aurora" ]; then
	name="su.xash.Engine"
	arch="armv7hl" # lame!
else
	name="harbour-xash3d-fwgs"
	arch="aarch64" # absolutely lame...
fi
package=$(ls build_rpm/RPMS/$arch/$name-0*.rpm)

sshpass -p $PASSWORD scp $package defaultuser@$IP:~/

# sandwich of programs:
# 1. Call sshpass to automatically fill password for ssh session
# 2. Call devel-su that reads password from stdin
# 3. devel-su calls pkcon install-local -y and installs the package
echo $PASSWORD | sshpass -p $PASSWORD ssh defaultuser@$IP devel-su pkcon install-local -y $(basename $package)
