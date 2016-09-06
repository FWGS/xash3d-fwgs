package in.celest.xash3d;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.opengles.GL10;
import javax.microedition.khronos.egl.*;

import android.app.*;
import android.content.*;
import android.view.*;
import android.os.*;
import android.util.*;
import android.graphics.*;
import android.text.method.*;
import android.text.*;
import android.media.*;
import android.hardware.*;
import android.content.*;
import android.widget.*;
import android.content.pm.*;

import android.view.inputmethod.*;

import java.lang.*;
import java.util.List;
import java.security.MessageDigest;

import in.celest.xash3d.hl.BuildConfig;
import in.celest.xash3d.XashConfig;

/**
 Xash Activity
 */
public class XashActivity extends Activity {

	// Main components
	protected static XashActivity mSingleton;
	protected static View mTextEdit;
	public static EngineSurface mSurface;
	public static String mArgv[];
	public static final int sdk = Integer.valueOf(Build.VERSION.SDK);
	public static final String TAG = "XASH3D:XashActivity";
	public static int mPixelFormat;
	protected static ViewGroup mLayout;
	public static JoystickHandler handler;
	public static ImmersiveMode mImmersiveMode;
	public static boolean keyboardVisible = false;

	// Joystick constants
	public final static byte JOY_HAT_CENTERED = 0; // bitmasks for hat current status
	public final static byte JOY_HAT_UP       = 1;
	public final static byte JOY_HAT_RIGHT    = 2;
	public final static byte JOY_HAT_DOWN     = 4;
	public final static byte JOY_HAT_LEFT     = 8;

	public final static byte JOY_AXIS_SIDE  = 0; // this represents default
	public final static byte JOY_AXIS_FWD   = 1; // engine binding: SFPYRL
	public final static byte JOY_AXIS_PITCH = 2;
	public final static byte JOY_AXIS_YAW   = 3;
	public final static byte JOY_AXIS_RT    = 4;
	public final static byte JOY_AXIS_LT    = 5;

	// Preferences
	public static SharedPreferences mPref = null;
	private static boolean mUseVolume;
	private static boolean mEnableImmersive;
	public static View mDecorView;

	// Audio
	private static Thread mAudioThread;
	private static AudioTrack mAudioTrack;
	
	// Certificate checking
	private static String SIG = "DMsE8f5hlR7211D8uehbFpbA0n8=";
	private static String SIG_TEST = ""; // a1ba: mittorn, add your signature later

	// Load the .so
	static {
		System.loadLibrary("xash");
	}

	// Shared between this activity and LauncherActivity
	public static boolean dumbAntiPDALifeCheck( Context context )
	{
		if( BuildConfig.DEBUG || 
			!XashConfig.CHECK_SIGNATURES )
			return false; // disable checking for debug builds
	
		try
		{
			PackageInfo info = context.getPackageManager()
				.getPackageInfo( context.getPackageName(), PackageManager.GET_SIGNATURES );
			
			for( Signature signature: info.signatures )
			{
				MessageDigest md = MessageDigest.getInstance( "SHA" );
				final byte[] signatureBytes = signature.toByteArray();

				md.update( signatureBytes );

				final String curSIG = Base64.encodeToString( md.digest(), Base64.NO_WRAP );

				if( XashConfig.PKG_TEST )
				{
					if( SIG_TEST.equals(curSIG) )
						return false;
				}
				else
				{
					if( SIG.equals(curSIG) )
						return false;
				}
			}
		} 
		catch( Exception e ) 
		{
			e.printStackTrace();
		}
		
		Log.e(TAG, "Please, don't resign our public release builds!");
		Log.e(TAG, "If you want to insert some features, rebuild package with ANOTHER package name from git repository.");
		return true;
	}
	
	// Setup
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		Log.v(TAG, "onCreate()");
		super.onCreate(savedInstanceState);
		
		if( dumbAntiPDALifeCheck(this) )
		{
			finish();
			return;
		}
		
		// So we can call stuff from static callbacks
		mSingleton = this;
		Intent intent = getIntent();

		// fullscreen
		requestWindowFeature(Window.FEATURE_NO_TITLE);
		//if(sdk >= 12)
		getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN, WindowManager.LayoutParams.FLAG_FULLSCREEN);

		// landscapeSensor is not supported until API9
		if( sdk < 9 )
			setRequestedOrientation(0);

		// keep screen on
		getWindow().setFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON, WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

		// Set up the surface
		mSurface = new EngineSurface(getApplication());

		if( sdk < 12 )
			handler = new JoystickHandler();
		else
			handler = new JoystickHandler_v12();
		handler.init();

		mLayout = new FrameLayout(this);
		mLayout.addView(mSurface);
		setContentView(mLayout);

		SurfaceHolder holder = mSurface.getHolder();
		holder.setType(SurfaceHolder.SURFACE_TYPE_GPU);

		// setup envs
		mPref = this.getSharedPreferences("engine", 0);
		String argv = intent.getStringExtra("argv");
		if(argv == null) argv = mPref.getString("argv", "-dev 3 -log");
		if(argv == null) argv = "-dev 3 -log";
		mArgv= argv.split(" ");

		String gamelibdir = intent.getStringExtra("gamelibdir");
		if(gamelibdir == null)
			gamelibdir = getFilesDir().getParentFile().getPath() + "/lib";

		String gamedir = intent.getStringExtra("gamedir");
		if(gamedir == null)
			gamedir = "valve";

		String basedir = intent.getStringExtra("basedir");
		if(basedir == null)
			basedir = mPref.getString("basedir","/sdcard/xash/");

		setenv("XASH3D_BASEDIR", basedir, true);
		setenv("XASH3D_ENGLIBDIR", getFilesDir().getParentFile().getPath() + "/lib", true);
		setenv("XASH3D_GAMELIBDIR", gamelibdir, true);
		setenv("XASH3D_GAMEDIR", gamedir, true);


		setenv("XASH3D_EXTRAS_PAK1", getFilesDir().getPath() + "/extras.pak", true);
		String pakfile = intent.getStringExtra("pakfile");
		if( pakfile != null && pakfile != "" )
			setenv("XASH3D_EXTRAS_PAK2", pakfile, true);
		String[] env = intent.getStringArrayExtra("env");
		try
		{
			if( env != null )
			for(int i = 0; i+1 < env.length; i+=2)
			{
				setenv(env[i],env[i+1], true);
			}
		}
		catch(Exception e)
		{
			e.printStackTrace();
		}

		InstallReceiver.extractPAK(this, false);

		mPixelFormat = mPref.getInt("pixelformat", 0);
		mUseVolume = mPref.getBoolean("usevolume", false);
		if( mPref.getBoolean("enableResizeWorkaround", true) )
			AndroidBug5497Workaround.assistActivity(this);
		
		// Immersive Mode is available only at >KitKat
		mEnableImmersive = (sdk >= 19 && mPref.getBoolean("immersive_mode", true));
		if( mEnableImmersive )
			mImmersiveMode = new ImmersiveMode_v19();
		mDecorView = getWindow().getDecorView();
	}

	// Events
	@Override
	protected void onPause() {
		Log.v(TAG, "onPause()");
		super.onPause();
	}

	@Override
	protected void onResume() {
		Log.v(TAG, "onResume()");
		super.onResume();
	}
	
	@Override
	public void onWindowFocusChanged(boolean hasFocus) 
	{
		super.onWindowFocusChanged(hasFocus);

		if( mImmersiveMode != null )
			mImmersiveMode.apply();
	}	

	public static native int nativeInit(Object arguments);
	public static native void nativeQuit();
	public static native void onNativeResize(int x, int y);
	public static native void nativeTouch(int pointerFingerId, int action, float x, float y);
	public static native void nativeKey( int down, int code );
	public static native void nativeString( String text );
	public static native void nativeSetPause(int pause);

	public static native void nativeHat(int id, byte hat, byte keycode, boolean down);
	public static native void nativeAxis(int id, byte axis, short value);
	public static native void nativeJoyButton(int id, byte button, boolean down);
	
	// for future expansion
	public static native void nativeBall(int id, byte ball, short xrel, short yrel);
	public static native void nativeJoyAdd( int id );
	public static native void nativeJoyDel( int id );
	
	public static native int setenv(String key, String value, boolean overwrite);

	// Java functions called from C

	public static boolean createGLContext() {
		return mSurface.InitGL();
	}

	public static void swapBuffers() {
		mSurface.SwapBuffers();
	}

	public static Surface getNativeSurface() {
		return XashActivity.mSurface.getNativeSurface();
	}

	public static void vibrate( int time ) {
		((Vibrator) mSingleton.getSystemService(Context.VIBRATOR_SERVICE)).vibrate( time );
	}

	public static void toggleEGL(int toggle) {
		mSurface.toggleEGL(toggle);
	}
	
	public static boolean deleteGLContext() {
		mSurface.ShutdownGL();
		return true;
	}

	public static Context getContext() {
		return mSingleton;
	}
	protected final String[] messageboxData = new String[2];
	public static void messageBox(String title, String text)
	{
		mSingleton.messageboxData[0] = title;
		mSingleton.messageboxData[1] = text;
		mSingleton.runOnUiThread(new Runnable() {
		@Override
		public void run()
		{
			new AlertDialog.Builder(mSingleton)
				.setTitle(mSingleton.messageboxData[0])
				.setMessage(mSingleton.messageboxData[1])
				.setPositiveButton( "Ok", new DialogInterface.OnClickListener() {
					public void onClick(DialogInterface dialog, int whichButton) {
						synchronized(mSingleton.messageboxData)
						{
							mSingleton.messageboxData.notify();
						}
					}
				})
				.setCancelable(false)
				.show();
			}
		});
		synchronized (mSingleton.messageboxData) {
			try {
				mSingleton.messageboxData.wait();
			} catch (InterruptedException ex) {
				ex.printStackTrace();
			}
		}
	}

	public static boolean handleKey( int keyCode, KeyEvent event )
	{
		if ( mUseVolume && ( keyCode == KeyEvent.KEYCODE_VOLUME_DOWN ||
			keyCode == KeyEvent.KEYCODE_VOLUME_UP ) )
			return false;
			
		final int source = XashActivity.handler.getSource(event);
		final int action = event.getAction();
		final boolean isGamePad  = (source & InputDevice.SOURCE_GAMEPAD)        == InputDevice.SOURCE_GAMEPAD;
		final boolean isJoystick = (source & InputDevice.SOURCE_CLASS_JOYSTICK) == InputDevice.SOURCE_CLASS_JOYSTICK;
		final boolean isDPad     = (source & InputDevice.SOURCE_DPAD)           == InputDevice.SOURCE_DPAD;

		if( isDPad )
		{
			byte val;
			final byte hat = 0;
			final int id = 0;
			Log.d(TAG, "DPAD button: " + keyCode );
			switch( keyCode )
			{
			case KeyEvent.KEYCODE_DPAD_CENTER: val = JOY_HAT_CENTERED; break;
			case KeyEvent.KEYCODE_DPAD_UP:     val = JOY_HAT_UP;       break;
			case KeyEvent.KEYCODE_DPAD_RIGHT:  val = JOY_HAT_RIGHT;    break;
			case KeyEvent.KEYCODE_DPAD_DOWN:   val = JOY_HAT_DOWN;     break;
			case KeyEvent.KEYCODE_DPAD_LEFT:   val = JOY_HAT_LEFT;     break;
			default: return performEngineKeyEvent( action, keyCode, event );
			}

			if(action == KeyEvent.ACTION_DOWN)
				nativeHat(id, hat, val, true);
			else if(action == KeyEvent.ACTION_UP)
				nativeHat(id, hat, val, false);

			return true;
		}

		// Engine will bind these to AUX${val} virtual keys
		// Android may send event without source flags set to GAMEPAD or CLASS_JOYSTICK
		// so check for gamepad buttons anyway
		if( isGamePad || isJoystick || XashActivity.handler.isGamepadButton( keyCode ) )
		{
			final int id = 0;
			byte val = 15;
			
			switch( keyCode )
			{
			// main buttons. DONT CHANGE THIS!!!!111oneone
			case KeyEvent.KEYCODE_BUTTON_A:      val = 0; break;
			case KeyEvent.KEYCODE_BUTTON_B:	     val = 1; break;
			case KeyEvent.KEYCODE_BUTTON_X:	     val = 2; break;
			case KeyEvent.KEYCODE_BUTTON_Y:	     val = 3; break;
			case KeyEvent.KEYCODE_BUTTON_L1:     val = 4; break;
			case KeyEvent.KEYCODE_BUTTON_R1:     val = 5; break;
			case KeyEvent.KEYCODE_BUTTON_SELECT: val = 6; break;
			case KeyEvent.KEYCODE_BUTTON_MODE:   val = 7; break;
			case KeyEvent.KEYCODE_BUTTON_START:  val = 8; break;
			case KeyEvent.KEYCODE_BUTTON_THUMBL: val = 9; break;
			case KeyEvent.KEYCODE_BUTTON_THUMBR: val = 10; break;
			
			// other
			case KeyEvent.KEYCODE_BUTTON_C:  val = 11; break;
			case KeyEvent.KEYCODE_BUTTON_Z:  val = 12; break;
			case KeyEvent.KEYCODE_BUTTON_L2: val = 13; break;
			case KeyEvent.KEYCODE_BUTTON_R2: val = 14; break;
			default: 
				if( keyCode >= KeyEvent.KEYCODE_BUTTON_1 && keyCode <= KeyEvent.KEYCODE_BUTTON_16 )
				{
					val = (byte)((keyCode - KeyEvent.KEYCODE_BUTTON_1) + 15);
				}
				else if( XashActivity.handler.isGamepadButton(keyCode) )
				{
					// maybe never reached, as all possible gamepad buttons are checked before
					Log.d(TAG, "Unhandled GamePad button: " + XashActivity.handler.keyCodeToString(keyCode) );
					return false;
				}
				else
				{
					// must be never reached too
					return performEngineKeyEvent( action, keyCode, event );
				}
			}

			if( event.getAction() == KeyEvent.ACTION_DOWN )
			{
				nativeJoyButton( id, val, true );
				return true;
			}
			else if( event.getAction() == KeyEvent.ACTION_UP )
			{
				nativeJoyButton( id, val, false );
				return true;
			}
			return false;
		}

		return performEngineKeyEvent( action, keyCode, event );
	}

	public static boolean performEngineKeyEvent( int action, int keyCode, KeyEvent event)
	{
		if( action == KeyEvent.ACTION_DOWN )
		{
			if( event.isPrintingKey() || keyCode == 62 )// space is printing too
				XashActivity.nativeString( String.valueOf( (char) event.getUnicodeChar() ) );

			XashActivity.nativeKey( 1, keyCode );

			return true;
		}
		else if( action == KeyEvent.ACTION_UP )
		{
			XashActivity.nativeKey( 0, keyCode );
			return true;
		}
		return false;
	}
	
	public static float performEngineAxisEvent( float current, byte engineAxis, float prev, float flat )
	{
		if( prev != current )
		{
			final int id = 0;
			final short SHRT_MAX = 32767;
			if( current <= flat && current >= -flat )
				current = 0;
			
			nativeAxis( id, engineAxis, (short)(current * SHRT_MAX) );
		}
		
		return current;
	}
	
	public static float performEngineHatEvent( float curr, boolean isXAxis, float prev )
	{
		if( prev != curr )
		{
			final int id = 0;
			final byte hat = 0;
			if( isXAxis )
			{
				     if( curr > 0 ) nativeHat( id, hat, JOY_HAT_RIGHT, true );
				else if( curr < 0 ) nativeHat( id, hat, JOY_HAT_LEFT,  true );
				// unpress previous if curr centered
				else if( prev > 0 ) nativeHat( id, hat, JOY_HAT_RIGHT, false );
				else if( prev < 0 ) nativeHat( id, hat, JOY_HAT_LEFT,  false );
			}
			else
			{
				     if( curr > 0 ) nativeHat( id, hat, JOY_HAT_DOWN, true );
				else if( curr < 0 ) nativeHat( id, hat, JOY_HAT_UP,   true );
				// unpress previous if curr centered
				else if( prev > 0 ) nativeHat( id, hat, JOY_HAT_DOWN, false );
				else if( prev < 0 ) nativeHat( id, hat, JOY_HAT_UP,   false );
			}
		}
		return curr;
	}

	static class ShowTextInputTask implements Runnable
	{
		/*
		 * This is used to regulate the pan&scan method to have some offset from
		 * the bottom edge of the input region and the top edge of an input
		 * method (soft keyboard)
		 */
		private int show;

		public ShowTextInputTask(int show1) {
		   show = show1;
		}

		@Override
		public void run() {
		   	InputMethodManager imm = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);


			if (mTextEdit == null)
			{
				mTextEdit = new DummyEdit(getContext());
				mLayout.addView(mTextEdit);
			}
			if( show == 1 )
			{
				mTextEdit.setVisibility(View.VISIBLE);
				mTextEdit.requestFocus();

				imm.showSoftInput(mTextEdit, 0);
				keyboardVisible = true;
				if( XashActivity.mImmersiveMode != null )
					XashActivity.mImmersiveMode.apply();
			}
			else
			{
				mTextEdit.setVisibility(View.GONE);
				imm.hideSoftInputFromWindow(mTextEdit.getWindowToken(), 0);
				keyboardVisible = false;
				if( XashActivity.mImmersiveMode != null )
					XashActivity.mImmersiveMode.apply();
			}
		}
	}

	/**
	 * This method is called by engine using JNI.
	 */
	public static void showKeyboard( int show )
	{
		// Transfer the task to the main thread as a Runnable
		mSingleton.runOnUiThread(new ShowTextInputTask(show));
	}

}

/**
 Simple nativeInit() runnable
 */
class XashMain implements Runnable {
	public void run()
	{
		XashActivity.nativeInit(XashActivity.mArgv);
	}
}

/**
 EngineSurface. This is what we draw on, so we need to know when it's created
 in order to do anything useful.

 Because of this, that's where we set up the Xash3D thread
 */
class EngineSurface extends SurfaceView implements SurfaceHolder.Callback,
View.OnKeyListener {

	// This is what Xash3D runs in. It invokes main(), eventually
	private static Thread mEngThread = null;

	// EGL private objects
	private EGLContext  mEGLContext;
	private EGLSurface  mEGLSurface;
	private EGLDisplay  mEGLDisplay;
	private EGL10 mEGL;
	private EGLConfig mEGLConfig;
	public static final String TAG = "XASH3D-EngineSurface";

	// Sensors

	// Startup
	public EngineSurface(Context context)
	{
		super(context);
		getHolder().addCallback(this);

		setFocusable(true);
		setFocusableInTouchMode(true);
		requestFocus();
		setOnKeyListener(this);
		if( XashActivity.sdk >= 5 )
			setOnTouchListener(new EngineTouchListener_v5());
		else
			setOnTouchListener(new EngineTouchListener_v1());
	}

	// Called when we have a valid drawing surface
	public void surfaceCreated(SurfaceHolder holder)
	{
		Log.v(TAG, "surfaceCreated()");
		if( mEGL == null )
			return;
		XashActivity.nativeSetPause(0);
	}

	// Called when we lose the surface
	public void surfaceDestroyed(SurfaceHolder holder)
	{
		Log.v(TAG, "surfaceDestroyed()");
		if( mEGL == null )
			return;
		XashActivity.nativeSetPause(1);
	}

	// Called when the surface is resized
	public void surfaceChanged(SurfaceHolder holder, int format, int width, int height)
	{
		Log.v(TAG, "surfaceChanged()");

		XashActivity.onNativeResize(width, height);
		// Now start up the C app thread
		if (mEngThread == null) {

			mEngThread = new Thread(new XashMain(), "EngineThread");
			mEngThread.start();
		}
	}

	// unused
	public void onDraw(Canvas canvas) {}

	// first, initialize native backend
	public Surface getNativeSurface() {
		return getHolder().getSurface();
	}

	// EGL functions
	public boolean InitGL() {
		try
		{
			EGL10 egl = (EGL10)EGLContext.getEGL();

			EGLDisplay dpy = egl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY);

			int[] version = new int[2];
			egl.eglInitialize(dpy, version);

			int[][] configSpec = {{
				EGL10.EGL_DEPTH_SIZE,   8,
				EGL10.EGL_RED_SIZE,   8,
				EGL10.EGL_GREEN_SIZE,  8,
				EGL10.EGL_BLUE_SIZE,   8,
				EGL10.EGL_ALPHA_SIZE, 8,
				EGL10.EGL_NONE
			}, {
				EGL10.EGL_DEPTH_SIZE,   8,
				EGL10.EGL_RED_SIZE,   8,
				EGL10.EGL_GREEN_SIZE,  8,
				EGL10.EGL_BLUE_SIZE,   8,
				EGL10.EGL_ALPHA_SIZE, 0,
				EGL10.EGL_NONE
			}, {
				EGL10.EGL_DEPTH_SIZE,   8,
				EGL10.EGL_RED_SIZE,   5,
				EGL10.EGL_GREEN_SIZE,  6,
				EGL10.EGL_BLUE_SIZE,   5,
				EGL10.EGL_ALPHA_SIZE, 0,
				EGL10.EGL_NONE
			}, {
				EGL10.EGL_DEPTH_SIZE,   8,
				EGL10.EGL_RED_SIZE,   5,
				EGL10.EGL_GREEN_SIZE,  5,
				EGL10.EGL_BLUE_SIZE,   5,
				EGL10.EGL_ALPHA_SIZE, 1,

				EGL10.EGL_NONE
			}, {
				EGL10.EGL_DEPTH_SIZE,   8,
				EGL10.EGL_RED_SIZE,   4,
				EGL10.EGL_GREEN_SIZE,  4,
				EGL10.EGL_BLUE_SIZE,   4,
				EGL10.EGL_ALPHA_SIZE,   4,

				EGL10.EGL_NONE
			}, {
				EGL10.EGL_DEPTH_SIZE,   8,
				EGL10.EGL_RED_SIZE,   3,
				EGL10.EGL_GREEN_SIZE,  3,
				EGL10.EGL_BLUE_SIZE,   2,
				EGL10.EGL_ALPHA_SIZE,   0,

				EGL10.EGL_NONE
			}};
			EGLConfig[] configs = new EGLConfig[1];
			int[] num_config = new int[1];
			if (!egl.eglChooseConfig(dpy, configSpec[XashActivity.mPixelFormat], configs, 1, num_config) || num_config[0] == 0)
			{
				Log.e(TAG, "No EGL config available");
				return false;
			}
			EGLConfig config = configs[0];

			int EGL_CONTEXT_CLIENT_VERSION=0x3098;
			int contextAttrs[] = new int[]
			{
				EGL_CONTEXT_CLIENT_VERSION, 1,
				EGL10.EGL_NONE
			};
			EGLContext ctx = egl.eglCreateContext(dpy, config, EGL10.EGL_NO_CONTEXT, contextAttrs);
			if (ctx == EGL10.EGL_NO_CONTEXT) {
				Log.e(TAG, "Couldn't create context");
				return false;
			}

			EGLSurface surface = egl.eglCreateWindowSurface(dpy, config, this, null);
			if (surface == EGL10.EGL_NO_SURFACE) {
				Log.e(TAG, "Couldn't create surface");
				return false;
			}

			if (!egl.eglMakeCurrent(dpy, surface, surface, ctx)) {
				Log.e(TAG, "Couldn't make context current");
				return false;
			}

			mEGLContext = ctx;
			mEGLDisplay = dpy;
			mEGLSurface = surface;
			mEGL = egl;
			mEGLConfig = config;

		} catch(Exception e) {
			Log.v(TAG, e + "");
			for (StackTraceElement s : e.getStackTrace()) {
				Log.v(TAG, s.toString());
			}
		}

		return true;
	}

	// EGL buffer flip
	public void SwapBuffers()
	{
		if( mEGLSurface == null )
			return;
		mEGL.eglSwapBuffers(mEGLDisplay, mEGLSurface);
	}
	public void toggleEGL(int toggle)
	{
	   if( toggle != 0 )
	   {
			mEGLSurface = mEGL.eglCreateWindowSurface(mEGLDisplay, mEGLConfig, this, null);
			mEGL.eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface, mEGLContext);
	   }
	   else
	   {
			mEGL.eglMakeCurrent(mEGLDisplay, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_CONTEXT);
			mEGL.eglDestroySurface(mEGLDisplay, mEGLSurface);
			mEGLSurface = null;
	   }
	}
	public void ShutdownGL()
	{
		mEGL.eglDestroyContext(mEGLDisplay, mEGLContext);
		mEGLContext = null;
		
		mEGL.eglDestroySurface(mEGLDisplay, mEGLSurface);
		mEGLSurface = null;
		
		mEGL.eglTerminate(mEGLDisplay);
		mEGLDisplay = null;
	}


	@Override
	public boolean onKey(View v, int keyCode, KeyEvent event) 
	{
		return XashActivity.handleKey( keyCode, event );
	}

}

/* This is a fake invisible editor view that receives the input and defines the
 * pan&scan region
 */
class DummyEdit extends View implements View.OnKeyListener {
	InputConnection ic;

	public DummyEdit(Context context) {
		super(context);
		setFocusableInTouchMode(true);
		setFocusable(true);
		setOnKeyListener(this);
	}

	@Override
	public boolean onCheckIsTextEditor() {
		return true;
	}

	@Override
	public boolean onKey(View v, int keyCode, KeyEvent event) {

		return XashActivity.handleKey( keyCode, event );
	}

	@Override
	public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
		ic = new XashInputConnection(this, true);

		outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI
				| 33554432 /* API 11: EditorInfo.IME_FLAG_NO_FULLSCREEN */;

		return ic;
	}
}

class XashInputConnection extends BaseInputConnection {

	public XashInputConnection(View targetView, boolean fullEditor) {
		super(targetView, fullEditor);

	}

	@Override
	public boolean sendKeyEvent(KeyEvent event) {

		if( XashActivity.handleKey( event.getKeyCode(), event ) )
			return true;
		return super.sendKeyEvent(event);
	}

	@Override
	public boolean commitText(CharSequence text, int newCursorPosition) {

		//nativeCommitText(text.toString(), newCursorPosition);
		if(text.toString().equals("\n"))
		{
			XashActivity.nativeKey(KeyEvent.KEYCODE_ENTER,1);
			XashActivity.nativeKey(KeyEvent.KEYCODE_ENTER,0);
		}
		XashActivity.nativeString(text.toString());

		return super.commitText(text, newCursorPosition);
	}

	@Override
	public boolean setComposingText(CharSequence text, int newCursorPosition) {

		//nativeSetComposingText(text.toString(), newCursorPosition);
		XashActivity.nativeString(text.toString());

		return super.setComposingText(text, newCursorPosition);
	}

	public native void nativeSetComposingText(String text, int newCursorPosition);

	@Override
	public boolean deleteSurroundingText(int beforeLength, int afterLength) {
		// Workaround to capture backspace key. Ref: http://stackoverflow.com/questions/14560344/android-backspace-in-webview-baseinputconnection
		if (beforeLength == 1 && afterLength == 0) {

			// backspace
			XashActivity.nativeKey(1,KeyEvent.KEYCODE_DEL);
			XashActivity.nativeKey(0,KeyEvent.KEYCODE_DEL);
		}

		return super.deleteSurroundingText(beforeLength, afterLength);
	}
}
class EngineTouchListener_v1 implements View.OnTouchListener{
	// Touch events
	public boolean onTouch(View v, MotionEvent event)
	{
		XashActivity.nativeTouch(0, event.getAction(), event.getX(), event.getY());
		return true;
	}
}

class EngineTouchListener_v5 implements View.OnTouchListener{
	// Touch events
	public boolean onTouch(View v, MotionEvent event)
	{
		final int touchDevId = event.getDeviceId();
		final int pointerCount = event.getPointerCount();
		int action = event.getActionMasked();
		int pointerFingerId;
		int mouseButton;
		int i = -1;
		float x,y;
			switch(action) {
				case MotionEvent.ACTION_MOVE:
					for (i = 0; i < pointerCount; i++) {
						pointerFingerId = event.getPointerId(i);
						x = event.getX(i);
						y = event.getY(i);
						XashActivity.nativeTouch(pointerFingerId, 2, x, y);
					}
					break;

				case MotionEvent.ACTION_UP:
				case MotionEvent.ACTION_DOWN:
					// Primary pointer up/down, the index is always zero
					i = 0;
				case MotionEvent.ACTION_POINTER_UP:
				case MotionEvent.ACTION_POINTER_DOWN:
					// Non primary pointer up/down
					if (i == -1) {
						i = event.getActionIndex();
					}

					pointerFingerId = event.getPointerId(i);

					x = event.getX(i);
					y = event.getY(i);
					if( action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP )
						XashActivity.nativeTouch(pointerFingerId,1, x, y);
					if( action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN )
						XashActivity.nativeTouch(pointerFingerId,0, x, y);
					break;

				case MotionEvent.ACTION_CANCEL:
					for (i = 0; i < pointerCount; i++) {
						pointerFingerId = event.getPointerId(i);
						x = event.getX(i);
						y = event.getY(i);
					   XashActivity.nativeTouch(pointerFingerId, 1, x, y);
					}
					break;

				default:
					break;
		 }
		return true;
	}
}
class AndroidBug5497Workaround {

	// For more information, see https://code.google.com/p/android/issues/detail?id=5497
	// To use this class, simply invoke assistActivity() on an Activity that already has its content view set.

	public static void assistActivity (Activity activity) {
		new AndroidBug5497Workaround(activity);
	}

	private View mChildOfContent;
	private int usableHeightPrevious;
	private FrameLayout.LayoutParams frameLayoutParams;

	private AndroidBug5497Workaround(Activity activity) {
		FrameLayout content = (FrameLayout) activity.findViewById(android.R.id.content);
		mChildOfContent = content.getChildAt(0);
		mChildOfContent.getViewTreeObserver().addOnGlobalLayoutListener(new ViewTreeObserver.OnGlobalLayoutListener() {
			public void onGlobalLayout() {
				possiblyResizeChildOfContent();
			}
		});
		frameLayoutParams = (FrameLayout.LayoutParams) mChildOfContent.getLayoutParams();
	}

	private void possiblyResizeChildOfContent() {
		int usableHeightNow = computeUsableHeight();
		if (usableHeightNow != usableHeightPrevious) {
			int usableHeightSansKeyboard = mChildOfContent.getRootView().getHeight();
			int heightDifference = usableHeightSansKeyboard - usableHeightNow;
			if (heightDifference > (usableHeightSansKeyboard/4)) {
				// keyboard probably just became visible
				frameLayoutParams.height = usableHeightSansKeyboard - heightDifference;
				XashActivity.keyboardVisible = true;
			} else {
				// keyboard probably just became hidden
				frameLayoutParams.height = usableHeightSansKeyboard;
				XashActivity.keyboardVisible = false;
			}
			if( XashActivity.mImmersiveMode != null )
				XashActivity.mImmersiveMode.apply();
			mChildOfContent.requestLayout();
			usableHeightPrevious = usableHeightNow;
		}
	}

	private int computeUsableHeight() {
		Rect r = new Rect();
		mChildOfContent.getWindowVisibleDisplayFrame(r);
		return (r.bottom - r.top);
	}

}

/*interface JoystickHandler
{
	public int getSource(KeyEvent event);
	public int getSource(MotionEvent event);
	public boolean handleAxis(MotionEvent event);
	public boolean isGamepadButton(int keyCode);
	public String keyCodeToString(int keyCode);
	public void init();
}*/
class JoystickHandler
{
	public int getSource(KeyEvent event)
	{
		return InputDevice.SOURCE_UNKNOWN;
	}
	public int getSource(MotionEvent event)
	{
		return InputDevice.SOURCE_UNKNOWN;
	}
	public boolean handleAxis(MotionEvent event)
	{
		return false;
	}
	public boolean isGamepadButton(int keyCode)
	{
		return false;
	}
	public String keyCodeToString(int keyCode)
	{
		return String.valueOf(keyCode);
	}
	public void init()
	{
	}
}

class JoystickHandler_v12 extends JoystickHandler
{


	private static float prevSide, prevFwd, prevYaw, prevPtch, prevLT, prevRT, prevHX, prevHY;

	public int getSource(KeyEvent event)
	{
		return event.getSource();
	}
	public int getSource(MotionEvent event)
	{
		return event.getSource();
	}
	public boolean handleAxis( MotionEvent event )
	{
		// maybe I need to cache this...
		for( InputDevice.MotionRange range: event.getDevice().getMotionRanges() )
		{
			// normalize in -1.0..1.0 (copied from SDL2)
			final float cur = ( event.getAxisValue( range.getAxis(), event.getActionIndex() ) - range.getMin() ) / range.getRange() * 2.0f - 1.0f;
			final float dead = range.getFlat(); // get axis dead zone
			switch( range.getAxis() )
			{
			// typical axes
			// move
			case MotionEvent.AXIS_X:   prevSide = XashActivity.performEngineAxisEvent(cur, XashActivity.JOY_AXIS_SIDE,  prevSide, dead); break;
			case MotionEvent.AXIS_Y:   prevFwd  = XashActivity.performEngineAxisEvent(cur, XashActivity.JOY_AXIS_FWD,   prevFwd,  dead); break;
			
			// rotate. Invert, so by default this works as it's should
			case MotionEvent.AXIS_Z:   prevPtch = XashActivity.performEngineAxisEvent(-cur, XashActivity.JOY_AXIS_PITCH, prevPtch, dead); break;
			case MotionEvent.AXIS_RZ:  prevYaw  = XashActivity.performEngineAxisEvent(-cur, XashActivity.JOY_AXIS_YAW,   prevYaw,  dead); break;
			
			// trigger
			case MotionEvent.AXIS_RTRIGGER:	prevLT = XashActivity.performEngineAxisEvent(cur, XashActivity.JOY_AXIS_RT, prevLT,   dead); break;
			case MotionEvent.AXIS_LTRIGGER:	prevRT = XashActivity.performEngineAxisEvent(cur, XashActivity.JOY_AXIS_LT, prevRT,   dead); break;
			
			// hats
			case MotionEvent.AXIS_HAT_X: prevHX = XashActivity.performEngineHatEvent(cur, true, prevHX); break;
			case MotionEvent.AXIS_HAT_Y: prevHY = XashActivity.performEngineHatEvent(cur, false, prevHY); break;
			}
		}

		return true;
	}
	public boolean isGamepadButton(int keyCode)
	{
		return KeyEvent.isGamepadButton(keyCode);
	}
	public String keyCodeToString(int keyCode)
	{
		return KeyEvent.keyCodeToString(keyCode);
	}
	public void init()
	{
		XashActivity.mSurface.setOnGenericMotionListener(new MotionListener());
	}

	class MotionListener implements View.OnGenericMotionListener
	{
		@Override
		public boolean onGenericMotion(View view, MotionEvent event)
		{
			if( ((XashActivity.handler.getSource(event) & InputDevice.SOURCE_CLASS_JOYSTICK) != 0) ||
				(XashActivity.handler.getSource(event) & InputDevice.SOURCE_GAMEPAD) != 0 )
				return XashActivity.handler.handleAxis( event );
			// TODO: Add it someday
			// else if( (event.getSource() & InputDevice.SOURCE_CLASS_TRACKBALL) == InputDevice.SOURCE_CLASS_TRACKBALL )
			//	return XashActivity.handleBall( event );
			//return super.onGenericMotion( view, event );
			return false;
		}
	}

}

class ImmersiveMode
{
	void apply()
	{
		//stub
	}
}

class ImmersiveMode_v19 extends ImmersiveMode
{
	@Override
	void apply()
	{
		if( !XashActivity.keyboardVisible )
			XashActivity.mDecorView.setSystemUiVisibility(
					0x00000100 // View.SYSTEM_UI_FLAG_LAYOUT_STABLE
					| 0x00000200 // View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
					| 0x00000400 // View.SYSTEM_UI_FLAG_LAYOUT_FULSCREEN
					| 0x00000002 // View.SYSTEM_UI_FLAG_HIDE_NAVIGATION // hide nav bar
					| 0x00000004 // View.SYSTEM_UI_FLAG_FULLSCREEN // hide status bar
					| 0x00001000 // View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
					);
		else
			XashActivity.mDecorView.setSystemUiVisibility( 0 );

	}
}
