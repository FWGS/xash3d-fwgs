#!/bin/bash

#cd into script directory
SCRIPTDIR=${0%/*}
cd "$SCRIPTDIR" || exit 1

#shouldn't build any mods that are supported by mobile_hacks
./buildhlsdk.sh aghl
./buildhlsdk.sh aomdc
./buildhlsdk.sh asheep
./buildhlsdk.sh blackops
./buildhlsdk.sh bubblemod
./buildhlsdk.sh CAd
./buildhlsdk.sh cracklife
./buildhlsdk.sh clcampaign
./buildhlsdk.sh dmc
./buildhlsdk.sh eftd
./buildhlsdk.sh decay-pc
./buildhlsdk.sh echoes
./buildhlsdk.sh gravgun
./buildhlsdk.sh opfor
./buildhlsdk.sh sci
./buildhlsdk.sh topdown
./buildhlsdk.sh half-screwed
./buildhlsdk.sh noffice
./buildhlsdk.sh poke646
./buildhlsdk.sh poke646_vendetta
./buildhlsdk.sh rebellion
./buildhlsdk.sh residual_point
./buildhlsdk.sh sohl1.2
./buildhlsdk.sh halloween
./buildhlsdk.sh thegate
./buildhlsdk.sh theyhunger
./buildhlsdk.sh zombie-x
./buildhlsdk.sh delta_particles
#untested if this works
export XENWARRIOR=ON
./buildhlsdk.sh sohl1.2 xenwar
./buildhlsdk.sh 

./createipa.sh
