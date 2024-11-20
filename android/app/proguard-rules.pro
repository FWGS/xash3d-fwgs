# SDL2 branch (unlike SDL3) doesn't have proguard file
# As SDL3 migration is planned, which we can easily do it on platforms
# unsupported by GoldSrc by the way, just prevent whole org.libsdl.app
# namespace from proguard optimization
-keep public class org.libsdl.app.* { public static *; }

-keep,includedescriptorclasses,allowoptimization class su.xash.engine.XashActivity
{
	java.lang.String loadAndroidID();
	java.lang.String getAndroidID();
	void saveAndroidID(java.lang.String);
	java.lang.String getCallingPackage();
	java.lang.String[] getAssetsList(boolean, java.lang.String);
	android.content.res.AssetManager getAssets(boolean);
}
