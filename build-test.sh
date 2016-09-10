ANDROID_JAR=../android-13.jar
AAPT=./../aapt
DX=./../dx
APKBUILDER=./../apkbuilder
mkdir gen
mkdir -p bin/classes

JAVAC=$JAVA_HOME/bin/javac

if [ -z "$JAVA_HOME" ]; then
    export JAVAC=$(which javac)
fi

: ${OUTPUT:=$1}
: ${OUTPUT:=bin/xash3d-signed.apk}
sh gen-version.sh test build
sh gen-config.sh test
rm assets/extras.pak
python2.7 makepak.py xash-extras assets/extras.pak
$AAPT package -m -J gen/ --rename-manifest-package in.celest.xash3d.hl -M AndroidManifest.xml -S test/res -I $ANDROID_JAR
echo "package in.celest.xash3d.hl;public final class BuildConfig {public final static boolean DEBUG = true;}" > gen/in/celest/xash3d/hl/BuildConfig.java
$JAVAC -d bin/classes -s bin/classes -cp $ANDROID_JAR gen/in/celest/xash3d/hl/*  src/in/celest/xash3d/*.java
$DX --dex --output=bin/classes.dex bin/classes/
$AAPT package -f -M test/AndroidManifest.xml -S test/res -I $ANDROID_JAR -F bin/xash3d.apk.unaligned
zip bin/xash3d.apk.unaligned assets/*
$APKBUILDER bin/xash3d.apk -u -nf libs/ -rj libs -f bin/classes.dex -z bin/xash3d.apk.unaligned
java -jar /mnt/app/apktool/signapk.jar /mnt/app/apktool/testkey.x509.pem /mnt/app/apktool/testkey.pk8 bin/xash3d.apk $OUTPUT
