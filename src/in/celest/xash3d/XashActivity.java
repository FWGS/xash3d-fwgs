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

import java.lang.*;

/**
 SDL Activity
 */
public class XashActivity extends Activity {

    // Main components
    protected static XashActivity mSingleton;
    private static EngineSurface mSurface;
    public static String mArgv[];
    public static final int sdk = Integer.valueOf(Build.VERSION.SDK);
    public static int mPixelFormat;

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
	      //Log.v("SDL", "onCreate()");
	      super.onCreate(savedInstanceState);

	      // So we can call stuff from static callbacks
	      mSingleton = this;
	      Intent intent = getIntent();

	      // fullscreen
		requestWindowFeature(Window.FEATURE_NO_TITLE);
		if(sdk >= 12) {
			getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
					WindowManager.LayoutParams.FLAG_FULLSCREEN);
		}

		// landscapeSensor is not supported until API9
		if( sdk < 9 )
			setRequestedOrientation(0);

		// keep screen on
		getWindow().setFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON,
				WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

		// Set up the surface
	      mSurface = new EngineSurface(getApplication());
	      setContentView(mSurface);
	      SurfaceHolder holder = mSurface.getHolder();
	      holder.setType(SurfaceHolder.SURFACE_TYPE_GPU);
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

		InstallReceiver.extractPAK(this, false);
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
		mPixelFormat = mPref.getInt("pixelformat", 0);
    }

    // Events
    protected void onPause() {
        //Log.v("SDL", "onPause()");
        super.onPause();
    }

    protected void onResume() {
        //Log.v("SDL", "onResume()");
        super.onResume();
    }

    // Messages from the SDLMain thread
    static int COMMAND_CHANGE_TITLE = 1;

    // Handler for the messages
    Handler commandHandler = new Handler() {
        public void handleMessage(Message msg) {
            if (msg.arg1 == COMMAND_CHANGE_TITLE) {
                setTitle((String)msg.obj);
            }
        }
    };

    // Send a message from the SDLMain thread
    void sendCommand(int command, Object data) {
        Message msg = commandHandler.obtainMessage();
        msg.arg1 = command;
        msg.obj = data;
        commandHandler.sendMessage(msg);
    }
    // C functions we call
    public static native int nativeInit(Object arguments);
    public static native void nativeQuit();
    public static native void onNativeResize(int x, int y);
    public static native void onNativeKeyDown(int keycode);
    public static native void onNativeKeyUp(int keycode);
    public static native void nativeTouch(int pointerFingerId,
                                            int action, float x,
                                            float y);
    public static native void nativeKey( int down, int code );
    public static native void nativeString( String text );
    public static native void onNativeAccel(float x, float y, float z);
    public static native void nativeRunAudioThread();
    public static native void nativeSetPause(int pause);

    public static native int setenv(String key, String value, boolean overwrite);


    // Java functions called from C

    public static boolean createGLContext() {
        return mSurface.InitGL();
    }

    public static void swapBuffers() {
        mSurface.SwapBuffers();
    }

    public static void toggleEGL(int toggle) {
        mSurface.toggleEGL(toggle);
    }

    public static void setActivityTitle(String title) {
        // Called from SDLMain() thread and can't directly affect the view
        mSingleton.sendCommand(COMMAND_CHANGE_TITLE, title);
    }

    public static Context getContext() {
        return mSingleton;
    }

    // Audio
    private static Object buf;

    public static Object audioInit(int sampleRate, boolean is16Bit, boolean isStereo, int desiredFrames) {
        int channelConfig = isStereo ? AudioFormat.CHANNEL_CONFIGURATION_STEREO : AudioFormat.CHANNEL_CONFIGURATION_MONO;
        int audioFormat = is16Bit ? AudioFormat.ENCODING_PCM_16BIT : AudioFormat.ENCODING_PCM_8BIT;
        int frameSize = (isStereo ? 2 : 1) * (is16Bit ? 2 : 1);

        Log.v("SDL", "SDL audio: wanted " + (isStereo ? "stereo" : "mono") + " " + (is16Bit ? "16-bit" : "8-bit") + " " + ((float)sampleRate / 1000f) + "kHz, " + desiredFrames + " frames buffer");

        // Let the user pick a larger buffer if they really want -- but ye
        // gods they probably shouldn't, the minimums are horrifyingly high
        // latency already
        desiredFrames = Math.max(desiredFrames, (AudioTrack.getMinBufferSize(sampleRate, channelConfig, audioFormat) + frameSize - 1) / frameSize);

        mAudioTrack = new AudioTrack(AudioManager.STREAM_MUSIC, sampleRate,
									 channelConfig, audioFormat, desiredFrames * frameSize, AudioTrack.MODE_STREAM);

        audioStartThread();

        Log.v("SDL", "SDL audio: got " + ((mAudioTrack.getChannelCount() >= 2) ? "stereo" : "mono") + " " + ((mAudioTrack.getAudioFormat() == AudioFormat.ENCODING_PCM_16BIT) ? "16-bit" : "8-bit") + " " + ((float)mAudioTrack.getSampleRate() / 1000f) + "kHz, " + desiredFrames + " frames buffer");

        if (is16Bit) {
            buf = new short[desiredFrames * (isStereo ? 2 : 1)];
        } else {
            buf = new byte[desiredFrames * (isStereo ? 2 : 1)];
        }
        return buf;
    }

    public static void audioStartThread() {
        mAudioThread = new Thread(new Runnable() {
				public void run() {
					mAudioTrack.play();
					nativeRunAudioThread();
				}
			});

        // I'd take REALTIME if I could get it!
        mAudioThread.setPriority(Thread.MAX_PRIORITY);
        mAudioThread.start();
    }

    public static void audioWriteShortBuffer(short[] buffer) {
        for (int i = 0; i < buffer.length; ) {
            int result = mAudioTrack.write(buffer, i, buffer.length - i);
            if (result > 0) {
                i += result;
            } else if (result == 0) {
                try {
                    Thread.sleep(1);
                } catch(InterruptedException e) {
                    // Nom nom
                }
            } else {
                Log.w("SDL", "SDL audio: error return from write(short)");
                return;
            }
        }
    }

    public static void audioWriteByteBuffer(byte[] buffer) {
        for (int i = 0; i < buffer.length; ) {
            int result = mAudioTrack.write(buffer, i, buffer.length - i);
            if (result > 0) {
                i += result;
            } else if (result == 0) {
                try {
                    Thread.sleep(1);
                } catch(InterruptedException e) {
                    // Nom nom
                }
            } else {
                Log.w("SDL", "SDL audio: error return from write(short)");
                return;
            }
        }
    }

    public static void audioQuit() {
        if (mAudioThread != null) {
            try {
                mAudioThread.join();
            } catch(Exception e) {
                Log.v("SDL", "Problem stopping audio thread: " + e);
            }
            mAudioThread = null;

            //Log.v("SDL", "Finished waiting for audio thread");
        }

        if (mAudioTrack != null) {
            mAudioTrack.stop();
            mAudioTrack = null;
        }
    }
}

/**
 Simple nativeInit() runnable
 */
class XashMain implements Runnable {
    public void run() {
        // Runs SDL_main()

	  XashActivity.createGLContext();

        XashActivity.nativeInit(XashActivity.mArgv);

        //Log.v("SDL", "SDL thread terminated");
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

    // Sensors

    // Startup
    public EngineSurface(Context context) {
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
    public void surfaceCreated(SurfaceHolder holder) {
        //Log.v("SDL", "surfaceCreated()");
        if( mEGL == null )
        	return;
        XashActivity.nativeSetPause(0);
    }

    // Called when we lose the surface
    public void surfaceDestroyed(SurfaceHolder holder) {
      if( mEGL == null )
        	return;
      XashActivity.nativeSetPause(1);
    }

    // Called when the surface is resized
    public void surfaceChanged(SurfaceHolder holder,
                               int format, int width, int height) {
        //Log.v("SDL", "surfaceChanged()");
/*
        int sdlFormat = 0x85151002; // SDL_PIXELFORMAT_RGB565 by default
        switch (format) {
			case PixelFormat.A_8:
				Log.v("SDL", "pixel format A_8");
				break;
			case PixelFormat.LA_88:
				Log.v("SDL", "pixel format LA_88");
				break;
			case PixelFormat.L_8:
				Log.v("SDL", "pixel format L_8");
				break;
			case PixelFormat.RGBA_4444:
				Log.v("SDL", "pixel format RGBA_4444");
				sdlFormat = 0x85421002; // SDL_PIXELFORMAT_RGBA4444
				break;
			case PixelFormat.RGBA_5551:
				Log.v("SDL", "pixel format RGBA_5551");
				sdlFormat = 0x85441002; // SDL_PIXELFORMAT_RGBA5551
				break;
			case PixelFormat.RGBA_8888:
				Log.v("SDL", "pixel format RGBA_8888");
				sdlFormat = 0x86462004; // SDL_PIXELFORMAT_RGBA8888
				break;
			case PixelFormat.RGBX_8888:
				Log.v("SDL", "pixel format RGBX_8888");
				sdlFormat = 0x86262004; // SDL_PIXELFORMAT_RGBX8888
				break;
			case PixelFormat.RGB_332:
				Log.v("SDL", "pixel format RGB_332");
				sdlFormat = 0x84110801; // SDL_PIXELFORMAT_RGB332
				break;
			case PixelFormat.RGB_565:
				Log.v("SDL", "pixel format RGB_565");
				sdlFormat = 0x85151002; // SDL_PIXELFORMAT_RGB565
				break;
			case PixelFormat.RGB_888:
				Log.v("SDL", "pixel format RGB_888");
				// Not sure this is right, maybe SDL_PIXELFORMAT_RGB24 instead?
				sdlFormat = 0x86161804; // SDL_PIXELFORMAT_RGB888
				break;
			default:
				Log.v("SDL", "pixel format unknown " + format);
				break;
        }*/


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

        try {
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
                Log.e("SDL", "No EGL config available");
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
                Log.e("SDL", "Couldn't create context");
                return false;
            }

            EGLSurface surface = egl.eglCreateWindowSurface(dpy, config, this, null);
            if (surface == EGL10.EGL_NO_SURFACE) {
                Log.e("SDL", "Couldn't create surface");
                return false;
            }

            if (!egl.eglMakeCurrent(dpy, surface, surface, ctx)) {
                Log.e("SDL", "Couldn't make context current");
                return false;
            }

            mEGLContext = ctx;
            mEGLDisplay = dpy;
            mEGLSurface = surface;
            mEGL = egl;
            mEGLConfig = config;

        } catch(Exception e) {
            Log.v("SDL", e + "");
            for (StackTraceElement s : e.getStackTrace()) {
                Log.v("SDL", s.toString());
            }
        }

        return true;
    }

    // EGL buffer flip
    public void SwapBuffers() {
       if( mEGLSurface == null )
       	return;
        try {
            //EGL10 egl = (EGL10)EGLContext.getEGL();

            //egl.eglWaitNative(EGL10.EGL_CORE_NATIVE_ENGINE, null);

            //egl.eglWaitGL();

            mEGL.eglSwapBuffers(mEGLDisplay, mEGLSurface);


        } catch(Exception e) {
            Log.v("SDL", "flipEGL(): " + e);
            for (StackTraceElement s : e.getStackTrace()) {
                Log.v("SDL", s.toString());
            }
        }
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

    // Key events
    public boolean onKey(View  v, int keyCode, KeyEvent event) {
		/*
		 * This handles the keycodes from soft keyboard (and IME-translated
		 * input from hardkeyboard)
		 */
		if (event.getAction() == KeyEvent.ACTION_DOWN) {
			if (event.isPrintingKey()|| keyCode == 62) {
				XashActivity.nativeString(String.valueOf((char) event.getUnicodeChar()));
			}
			XashActivity.nativeKey(1,keyCode);
			return true;
		} else if (event.getAction() == KeyEvent.ACTION_UP) {

			XashActivity.nativeKey(0,keyCode);
			return true;
		}
        return false;
    }
}
class EngineTouchListener_v1 implements View.OnTouchListener{
       // Touch events
    public boolean onTouch(View v, MotionEvent event) {
                XashActivity.nativeTouch(0, event.getAction(), event.getX(), event.getY());
		return true;
	}
}

class EngineTouchListener_v5 implements View.OnTouchListener{
       // Touch events
    public boolean onTouch(View v, MotionEvent event) {
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