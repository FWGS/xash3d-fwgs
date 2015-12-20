ANDROID_JAR=../android-13.jar
AAPT=./../aapt
DX=./../dx
APKBUILDER=./../apkbuilder
mkdir gen
mkdir bin
sh gen-version.sh test build
$AAPT package -m -J gen/ --rename-manifest-package in.celest.xash3d.hl -M AndroidManifest.xml -S test/res -I $ANDROID_JAR
$JAVA_HOME/bin/javac -d bin/classes -s bin/classes -cp $ANDROID_JAR src/org/libsdl/app/SDLActivity.java gen/in/celest/xash3d/hl/R.java src/in/celest/xash3d/*
$DX --dex --output=bin/classes.dex bin/classes/
/mnt/app/apktool/aapt package -f -M test/AndroidManifest.xml -S test/res -I $ANDROID_JAR -F bin/xash3d.apk.unaligned
zip bin/xash3d.apk.unaligned assets/*
$APKBUILDER bin/xash3d.apk -u -nf libs/ -rj libs -f bin/classes.dex -z bin/xash3d.apk.unaligned
java -jar /mnt/app/apktool/signapk.jar /mnt/app/apktool/testkey.x509.pem /mnt/app/apktool/testkey.pk8 bin/xash3d.apk bin/xash3d-signed.apk
