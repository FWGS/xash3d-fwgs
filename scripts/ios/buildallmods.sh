#cd into script directory
SCRIPTDIR=${0%/*}
cd $SCRIPTDIR

#shouldn't build any mods that are supported by mobile_hacks
./buildhlsdk.sh aghl _ag
./buildhlsdk.sh aomdc _AoMDC
./buildhlsdk.sh asheep _asheep
./buildhlsdk.sh blackops _blackops
./buildhlsdk.sh bubblemod _bubblemod
./buildhlsdk.sh CAd _CAd
./buildhlsdk.sh cracklife _cracklife
./buildhlsdk.sh clcampaign _clcampaign
./buildhlsdk.sh dmc _dmc
./buildhlsdk.sh eftd _eftd
#linux only??
#./buildhlsdk.sh decay-pc _decay
./buildhlsdk.sh echoes _echoes
./buildhlsdk.sh gravgun _gravgun
./buildhlsdk.sh opfor _opfor
./buildhlsdk.sh sci _sci
./buildhlsdk.sh topdown _topdown
./buildhlsdk.sh half-screwed _half-screwed
./buildhlsdk.sh noffice _noffice
./buildhlsdk.sh poke646 _poke646
./buildhlsdk.sh poke646_vendetta _poke646_vendetta
./buildhlsdk.sh rebellion _rebellion
./buildhlsdk.sh residual_point _rl
./buildhlsdk.sh residual_point _rp_v1_pub_final1
./buildhlsdk.sh sohl1.2 _Spirit
./buildhlsdk.sh halloween _shall1
./buildhlsdk.sh thegate _thegate
./buildhlsdk.sh theyhunger _hunger
./buildhlsdk.sh zombie-x _Zombie-X-DLE
#untested if this works
export XENWARRIOR=ON
./buildhlsdk.sh sohl1.2 _xenwar

./buildhlsdk.sh 


cd ../../
./createipa.sh