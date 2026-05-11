package su.xash.engine;

import android.annotation.SuppressLint;
import android.content.pm.ActivityInfo;
import android.content.res.AssetManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.preference.PreferenceManager;
import android.content.SharedPreferences;
import android.provider.Settings.Secure;
import android.util.Log;
import android.util.DisplayMetrics;
import android.view.KeyEvent;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import su.xash.engine.util.AndroidBug5497Workaround;

import java.io.File;
import java.util.Arrays;
import java.util.List;

public class XashActivity extends SDLActivity {
    private boolean mUseVolumeKeys;
    private String mPackageName;
    private static final String TAG = "XashActivity";
    private static final int MIN_SURFACE_WIDTH = 320;
    private static final int MIN_SURFACE_HEIGHT = 200;
    private SharedPreferences mPreferences;
    private String mCachedArgv;
    private int mFixedSurfaceWidth;
    private int mFixedSurfaceHeight;
    private boolean mStretchFixedSurface;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ensurePreferences();

        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }

        parseFixedResolution(getFinalArgv());
        applyFixedSurfaceSize();

        if (getBooleanPreference("keyboard_resizes_screen", true)) {
            AndroidBug5497Workaround.assistActivity(this);
        } else {
            getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyFixedSurfaceSize();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyFixedSurfaceSize();
        }
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        System.exit(0);
    }

    @Override
    protected String[] getLibraries() {
        return new String[]{"SDL2", "xash"};
    }

    @SuppressLint("HardwareIds")
    private String getAndroidID() {
        return Secure.getString(getContentResolver(), Secure.ANDROID_ID);
    }

    @SuppressLint("ApplySharedPref")
    private void saveAndroidID(String id) {
        getSharedPreferences("xash_preferences", MODE_PRIVATE).edit().putString("xash_id", id).commit();
    }

    private String loadAndroidID() {
        return getSharedPreferences("xash_preferences", MODE_PRIVATE).getString("xash_id", "");
    }

    @Override
    public String getCallingPackage() {
        if (mPackageName != null) {
            return mPackageName;
        }
        return super.getCallingPackage();
    }

    private AssetManager getAssets(boolean isEngine) {
        AssetManager am = null;
        if (isEngine) {
            am = getAssets();
        } else {
            try {
                am = getPackageManager().getResourcesForApplication(getCallingPackage()).getAssets();
            } catch (Exception e) {
                Log.e(TAG, "Unable to load mod assets!");
                e.printStackTrace();
            }
        }
        return am;
    }

    private String[] getAssetsList(boolean isEngine, String path) {
        AssetManager am = getAssets(isEngine);
        try {
            return am.list(path);
        } catch (Exception e) {
            e.printStackTrace();
        }
        return new String[]{};
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (SDLActivity.mBrokenLibraries) {
            return false;
        }
        int keyCode = event.getKeyCode();
        if (!mUseVolumeKeys) {
            if (keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || keyCode == KeyEvent.KEYCODE_VOLUME_UP || keyCode == KeyEvent.KEYCODE_CAMERA || keyCode == KeyEvent.KEYCODE_ZOOM_IN || keyCode == KeyEvent.KEYCODE_ZOOM_OUT) {
                return false;
            }
        }
        return getWindow().superDispatchKeyEvent(event);
    }

    private String getGlobalArguments() {
        ensurePreferences();
        String globalArgs = mPreferences.getString("global_arguments", "");
        if (globalArgs != null && !globalArgs.trim().isEmpty()) {
            return globalArgs.trim();
        }
        return "";
    }

    private void ensurePreferences() {
        if (mPreferences == null) {
            mPreferences = PreferenceManager.getDefaultSharedPreferences(this);
        }
    }

    private String combineArguments(String originalArgs, String globalArgs) {
        if (globalArgs.isEmpty()) {
            return originalArgs;
        }
        
        if (originalArgs == null || originalArgs.trim().isEmpty()) {
            return globalArgs;
        }
        
        return originalArgs.trim() + " " + globalArgs;
    }

    private String findBestBasedir(String gamedir) {
        File internalDir = new File(getExternalFilesDir(null).getAbsolutePath() + "/" + gamedir);
        if (internalDir.exists() && internalDir.isDirectory()) {
            Log.d(TAG, "Game found in internal storage: " + internalDir.getAbsolutePath());
            return getExternalFilesDir(null).getAbsolutePath();
        }
        
        File externalDir = new File(Environment.getExternalStorageDirectory().getAbsolutePath() + "/xash/" + gamedir);
        if (externalDir.exists() && externalDir.isDirectory()) {
            Log.d(TAG, "Game found in external storage: " + externalDir.getAbsolutePath());
            return Environment.getExternalStorageDirectory().getAbsolutePath() + "/xash";
        }
        
        boolean useInternalStorage = mPreferences.getBoolean("storage_toggle", false);
        if (useInternalStorage) {
            Log.d(TAG, "Game not found, using internal storage as default");
            return getExternalFilesDir(null).getAbsolutePath();
        } else {
            Log.d(TAG, "Game not found, using external storage as default");
            return Environment.getExternalStorageDirectory().getAbsolutePath() + "/xash";
        }
    }

    private String getFinalArgv() {
        if (mCachedArgv != null) {
            return mCachedArgv;
        }

        ensurePreferences();
        setStretchResolutionEnvironment();

        String rodir = getFilesDir().getAbsolutePath() + "/gamelibs";
        nativeSetenv("XASH3D_RODIR", rodir);
        Log.i(TAG, "XASH3D_RODIR = " + rodir);

        String gamedir = getIntent().getStringExtra("gamedir");
        if (gamedir == null) gamedir = "valve";
        
        String basedir = findBestBasedir(gamedir);
        nativeSetenv("XASH3D_BASEDIR", basedir);
        nativeSetenv("XASH3D_GAME", gamedir);
        
        Log.d(TAG, "Using basedir: " + basedir + " for game: " + gamedir);

        String gamelibdir = getIntent().getStringExtra("gamelibdir");
        if (gamelibdir != null) nativeSetenv("XASH3D_GAMELIBDIR", gamelibdir);

        String pakfile = getIntent().getStringExtra("pakfile");
        if (pakfile != null) nativeSetenv("XASH3D_EXTRAS_PAK2", pakfile);

        mUseVolumeKeys = getIntent().getBooleanExtra("usevolume", false);
        mPackageName = getIntent().getStringExtra("package");

        String[] env = getIntent().getStringArrayExtra("env");
        if (env != null) {
            for (int i = 0; i < env.length; i += 2)
                nativeSetenv(env[i], env[i + 1]);
        }

        String argv = getIntent().getStringExtra("argv");
        if (argv == null) argv = "-console -log";

        String globalArgs = getGlobalArguments();
        if (!globalArgs.isEmpty()) {
            Log.d(TAG, "Global arguments found: " + globalArgs);
            argv = combineArguments(argv, globalArgs);
        }

        if (!argv.contains("-game") && !gamedir.equals("valve")) {
            argv += " -game " + gamedir;
            Log.d(TAG, "Added -game parameter to argv: " + argv);
        }

        String resolutionArgs = getRenderResolutionArguments(argv);
        if (!resolutionArgs.isEmpty()) {
            argv += " " + resolutionArgs;
        }

        if (getBooleanPreference("stretch_resolution", false) && !hasArgument(argv, "-stretch_resolution")) {
            argv += " -stretch_resolution";
        }

        if (getBooleanPreference("fix_font", false) && !hasArgument(argv, "-fixfont")) {
            argv += " -fixfont";
        }

        if (!getBooleanPreference("keyboard_resizes_screen", true) && !hasArgument(argv, "-noresize")) {
            argv += " -noresize";
        }

        if (argv.indexOf(" -dll ") < 0 && gamelibdir == null) {
            final List<String> mobile_hacks_gamedirs = Arrays.asList(new String[]{
                "aom", "bdlands", "biglolly", "bshift", "caseclosed",
                "hl_urbicide", "induction", "redempt", "secret",
                "sewer_beta", "tot", "vendetta" });

            if (mobile_hacks_gamedirs.contains(gamedir))
                argv += " -dll @hl";
        }

        Log.d(TAG, "Final argv: " + argv);
        mCachedArgv = argv.trim();
        return mCachedArgv;
    }

    private String getRenderResolutionArguments(String argv) {
        if (hasArgument(argv, "-width") || hasArgument(argv, "-height")) {
            return "";
        }

        String resolution = mPreferences.getString("render_resolution", "");
        if (resolution == null || resolution.trim().isEmpty()) {
            return "";
        }

        String[] parts = resolution.trim().split("[xX,\\s]+");
        if (parts.length < 2) {
            Log.w(TAG, "Invalid render resolution: " + resolution);
            return "";
        }

        try {
            int width = Integer.parseInt(parts[0]);
            int height = Integer.parseInt(parts[1]);

            if (width < MIN_SURFACE_WIDTH || height < MIN_SURFACE_HEIGHT) {
                Log.w(TAG, "Render resolution is too small: " + resolution);
                return "";
            }

            return "-width " + width + " -height " + height;
        } catch (NumberFormatException e) {
            Log.w(TAG, "Invalid render resolution: " + resolution);
            return "";
        }
    }

    private void setStretchResolutionEnvironment() {
        boolean stretch = getBooleanPreference("stretch_resolution", false);
        nativeSetenv("XASH3D_STRETCH_RESOLUTION", stretch ? "1" : "0");

        if (!stretch) {
            return;
        }

        DisplayMetrics metrics = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(metrics);

        int nativeWidth = Math.max(metrics.widthPixels, metrics.heightPixels);
        int nativeHeight = Math.min(metrics.widthPixels, metrics.heightPixels);

        if (nativeWidth >= MIN_SURFACE_WIDTH && nativeHeight >= MIN_SURFACE_HEIGHT) {
            nativeSetenv("XASH3D_NATIVE_WIDTH", String.valueOf(nativeWidth));
            nativeSetenv("XASH3D_NATIVE_HEIGHT", String.valueOf(nativeHeight));
            Log.d(TAG, "Using native stretch size: " + nativeWidth + "x" + nativeHeight);
        }
    }

    private boolean hasArgument(String argv, String name) {
        if (argv == null || argv.isEmpty()) {
            return false;
        }

        String[] args = argv.trim().split("\\s+");
        for (String arg : args) {
            if (name.equals(arg)) {
                return true;
            }
        }

        return false;
    }

    private void parseFixedResolution(String argv) {
        mFixedSurfaceWidth = 0;
        mFixedSurfaceHeight = 0;

        if (argv == null || argv.isEmpty()) {
            return;
        }

        String[] args = argv.trim().split("\\s+");
        int width = getIntArgument(args, "-width");
        int height = getIntArgument(args, "-height");

        if (width >= MIN_SURFACE_WIDTH && height >= MIN_SURFACE_HEIGHT) {
            mFixedSurfaceWidth = width;
            mFixedSurfaceHeight = height;
            Log.d(TAG, "Using fixed Android surface size: " + width + "x" + height);
        }
    }

    private int getIntArgument(String[] args, String name) {
        for (int i = 0; i < args.length - 1; i++) {
            if (name.equals(args[i])) {
                try {
                    return Integer.parseInt(args[i + 1]);
                } catch (NumberFormatException e) {
                    Log.w(TAG, "Invalid " + name + " value: " + args[i + 1]);
                    return 0;
                }
            }
        }

        return 0;
    }

    private void applyFixedSurfaceSize() {
        if (mFixedSurfaceWidth <= 0 || mFixedSurfaceHeight <= 0) {
            return;
        }

        ensurePreferences();
        mStretchFixedSurface = getBooleanPreference("stretch_resolution", false);

        SurfaceView surfaceView = findSurfaceView(getWindow().getDecorView());
        if (surfaceView == null) {
            Log.w(TAG, "SDL surface not found; fixed resolution will be applied later if possible");
            return;
        }

        if (mStretchFixedSurface) {
            stretchSurfaceToScreen(surfaceView);
        } else {
            surfaceView.getHolder().setFixedSize(mFixedSurfaceWidth, mFixedSurfaceHeight);
        }
    }

    private void stretchSurfaceToScreen(SurfaceView surfaceView) {
        View view = surfaceView;
        while (view != null) {
            ViewGroup.LayoutParams params = view.getLayoutParams();
            if (params != null && (params.width != ViewGroup.LayoutParams.MATCH_PARENT || params.height != ViewGroup.LayoutParams.MATCH_PARENT)) {
                params.width = ViewGroup.LayoutParams.MATCH_PARENT;
                params.height = ViewGroup.LayoutParams.MATCH_PARENT;
                view.setLayoutParams(params);
            }

            if (!(view.getParent() instanceof View)) {
                break;
            }

            view = (View)view.getParent();
        }

        ViewGroup.LayoutParams params = surfaceView.getLayoutParams();
        if (params != null && (params.width != ViewGroup.LayoutParams.MATCH_PARENT || params.height != ViewGroup.LayoutParams.MATCH_PARENT)) {
            params.width = ViewGroup.LayoutParams.MATCH_PARENT;
            params.height = ViewGroup.LayoutParams.MATCH_PARENT;
            surfaceView.setLayoutParams(params);
        }

        surfaceView.setPivotX(0.0f);
        surfaceView.setPivotY(0.0f);
        surfaceView.setScaleX(1.0f);
        surfaceView.setScaleY(1.0f);
        surfaceView.requestLayout();
    }

    private SurfaceView findSurfaceView(View view) {
        if (view instanceof SurfaceView) {
            return (SurfaceView)view;
        }

        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup)view;
            for (int i = 0; i < group.getChildCount(); i++) {
                SurfaceView surface = findSurfaceView(group.getChildAt(i));
                if (surface != null) {
                    return surface;
                }
            }
        }

        return null;
    }

    private boolean getBooleanPreference(String key, boolean defaultValue) {
        if (mPreferences != null && mPreferences.contains(key)) {
            return mPreferences.getBoolean(key, defaultValue);
        }

        SharedPreferences appPreferences = getSharedPreferences("app_preferences", MODE_PRIVATE);
        if (appPreferences.contains(key)) {
            return appPreferences.getBoolean(key, defaultValue);
        }

        return defaultValue;
    }

    @Override
    protected String[] getArguments() {
        String argv = getFinalArgv();
        parseFixedResolution(argv);
        return argv.isEmpty() ? new String[]{} : argv.split("\\s+");
    }
}
