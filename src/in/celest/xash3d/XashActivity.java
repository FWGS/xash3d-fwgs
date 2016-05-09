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
import android.util.Log;
import android.graphics.*;
import android.text.method.*;
import android.text.*;
import android.media.*;
import android.hardware.*;
import android.content.*;
import android.widget.*;

import android.view.inputmethod.*;

import java.lang.*;

/**
 SDL Activity
 */
public class XashActivity extends Activity {

	// Main components
	protected static XashActivity mSingleton;
	protected static View mTextEdit;
	private static EngineSurface mSurface;
	public static String mArgv[];
	public static final int sdk = Integer.valueOf(Build.VERSION.SDK);
	public static final String TAG = "XASH3D:XashActivity";
	public static int mPixelFormat;
	protected static ViewGroup mLayout;

		// Preferences
	public static SharedPreferences mPref = null;

	// Audio
	private static Thread mAudioThread;
	private static AudioTrack mAudioTrack;
	// Load the .so
	static {
		System.loadLibrary("xash");
	}

	// Setup
	protected void onCreate(Bundle savedInstanceState) {
		Log.v(TAG, "onCreate()");
		super.onCreate(savedInstanceState);

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
		AndroidBug5497Workaround.assistActivity(this);
	}

	// Events
	protected void onPause() {
		Log.v(TAG, "onPause()");
		super.onPause();
	}

	protected void onResume() {
		Log.v(TAG, "onResume()");
		super.onResume();
	}

	public static native int nativeInit(Object arguments);
	public static native void nativeQuit();
	public static native void onNativeResize(int x, int y);
	public static native void nativeTouch(int pointerFingerId, int action, float x, float y);
	public static native void nativeKey( int down, int code );
	public static native void nativeString( String text );
	public static native void nativeSetPause(int pause);

	public static native int setenv(String key, String value, boolean overwrite);


	// Java functions called from C

	public static boolean createGLContext() {
		return mSurface.InitGL();
	}

	public static void swapBuffers() {
		mSurface.SwapBuffers();
	}

	public static void vibrate( int time ) {
		((Vibrator) mSingleton.getSystemService(Context.VIBRATOR_SERVICE)).vibrate( time );
	}

	public static void toggleEGL(int toggle) {
		mSurface.toggleEGL(toggle);
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

		//Log.d( TAG, "Keycode " + keyCode );
		if (event.getAction() == KeyEvent.ACTION_DOWN)
		{
			if (event.isPrintingKey() || keyCode == 62)// space is printing too
				XashActivity.nativeString(String.valueOf((char) event.getUnicodeChar()));

			XashActivity.nativeKey(1,keyCode);

			return true;
		}
		else if (event.getAction() == KeyEvent.ACTION_UP)
		{

			XashActivity.nativeKey(0,keyCode);
			return true;
		}
		return false;
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
			}
			else
			{
				mTextEdit.setVisibility(View.GONE);
				imm.hideSoftInputFromWindow(mTextEdit.getWindowToken(), 0);
			}
		}
	}

	/**
	 * This method is called by SDL using JNI.
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
		XashActivity.createGLContext();
		XashActivity.nativeInit(XashActivity.mArgv);
	}
}


/**
 SDLSurface. This is what we draw on, so we need to know when it's created
 in order to do anything useful.

 Because of this, that's where we set up the SDL thread
 */
class EngineSurface extends SurfaceView implements SurfaceHolder.Callback,
View.OnKeyListener {

	// This is what SDL runs in. It invokes SDL_main(), eventually
	private Thread mEngThread;

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


	@Override
	public boolean onKey(View v, int keyCode, KeyEvent event) {

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
			} else {
				// keyboard probably just became hidden
				frameLayoutParams.height = usableHeightSansKeyboard;
			}
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
