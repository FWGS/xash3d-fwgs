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
import android.net.Uri;
import android.provider.*;
import android.database.*;

import android.view.inputmethod.*;

import java.lang.*;
import java.lang.reflect.*;
import java.util.List;
import java.security.MessageDigest;

import in.celest.xash3d.hl.R;
import in.celest.xash3d.XashConfig;
import in.celest.xash3d.JoystickHandler;
import android.provider.Settings.Secure;

import su.xash.fwgslib.*;

/**
 Xash Activity
 */
public class XashActivity extends Activity {

	// Main components
	protected static XashActivity mSingleton;
	protected static View mTextEdit;
	protected static ViewGroup mLayout;
	public static EngineSurface mSurface;
	public static String mArgv[];
	public static final int sdk = Integer.valueOf( Build.VERSION.SDK );
	public static final String TAG = "XASH3D:XashActivity";
	public static int mPixelFormat;
	public static JoystickHandler handler;
	public static ImmersiveMode mImmersiveMode;
	public static boolean keyboardVisible = false;
	public static boolean mEngineReady = false;
	public static boolean mEnginePaused = false;
	public static Vibrator mVibrator;
	public static boolean fMouseShown = true;
	public static boolean fGDBSafe = false;
	public static float mScale = 0, mTouchScaleX = 1, mTouchScaleY = 1;
	public static int mForceHeight = 0, mForceWidth = 0;
	public static int mMinHeight = 240, mMinWidth = 320; // hl1 support 320 width, but mods may not.
	public static boolean bIsCstrike = false;

	private static boolean mHasVibrator;
	private int mReturingWithResultCode = 0;
	
	private static int OPEN_DOCUMENT_TREE_RESULT = 1;
	private static int FPICKER_RESULT = 2;
	

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
	public static View mDecorView;
	

	// Load the .so
	static 
	{
		System.loadLibrary( "gpgs_support" );
		System.loadLibrary( "xash" );
	}

	// Setup
	@Override
	protected void onCreate( Bundle savedInstanceState ) 
	{
		Log.v( TAG, "onCreate()" );
		super.onCreate( savedInstanceState );
		mEngineReady = false;
		
		if( CertCheck.dumbAntiPDALifeCheck( this ) )
		{
			finish();
			return;
		}
		
		// So we can call stuff from static callbacks
		mSingleton = this;

		// fullscreen
		requestWindowFeature( Window.FEATURE_NO_TITLE );
		
		int flags = WindowManager.LayoutParams.FLAG_FULLSCREEN | 
			WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON;
		getWindow().setFlags( flags, flags );

		// landscapeSensor is not supported until API9
		if( sdk < 9 )
			setRequestedOrientation( 0 );
			
		mPref = this.getSharedPreferences( "engine", 0 );
		
		if( mPref.getBoolean( "folderask", true ) )
		{
			Log.v( TAG, "folderask == true. Opening FPicker..." );
		
			Intent intent = new Intent( this, in.celest.xash3d.FPicker.class );
			startActivityForResult( intent, FPICKER_RESULT );
		}
		else
		{
			Log.v( TAG, "folderask == false. Checking write permission..." );

			// check write permission and run engine, if possible
			String basedir = getStringExtraFromIntent( getIntent(), "basedir", mPref.getString( "basedir", "/sdcard/xash/" ) );
			checkWritePermission( basedir );
		}
	}
	
	@Override
	public void onActivityResult( int requestCode, int resultCode, Intent resultData ) 
	{
		if( resultCode != RESULT_OK )
		{
			Log.v( TAG, "onActivityResult: result is not OK. ReqCode: " + requestCode + ". ResCode: " + resultCode );
		}
		else
		{
			// it's not possible to create dialogs here
			// so most work will be done after Activity resuming, in onPostResume()
			mReturingWithResultCode = requestCode;
			if( requestCode == FPICKER_RESULT )
			{
				String newBaseDir = resultData.getStringExtra( "GetPath" );
				setNewBasedir( newBaseDir );
				setFolderAsk( this, false ); // don't ask on next run
				Log.v( TAG, "Got new basedir from FPicker: " + newBaseDir );
			}
		}
	}
	
	@Override
	public void onPostResume()
	{
		super.onPostResume();
		
		if( mReturingWithResultCode != 0 )
		{
			if( mReturingWithResultCode == FPICKER_RESULT )
			{
				String basedir = mPref.getString( "basedir", "/sdcard/xash/" );
				checkWritePermission( basedir );
			}
			/*else if( mReturingWithResultCode == OPEN_DOCUMENT_TREE_RESULT )
			{
				String basedir = getStringExtraFromIntent( getIntent(), "basedir", mPref.getString("basedir","/sdcard/xash/"));
				Log.v(TAG, "Got permissions. Checking writing again...");

				if( nativeTestWritePermission( basedir ) == 0 )
				{
					Log.v(TAG, "Write test has failed twice!");
					String msg = getString(R.string.lollipop_request_permission_fail_msg) + getString(R.string.ask_about_new_basedir_msg);
			
					new AlertDialog.Builder(this)
						.setTitle( R.string.write_failed )
						.setMessage( msg )
						.setPositiveButton( R.string.ok, new DialogInterface.OnClickListener() 
							{
								public void onClick(DialogInterface dialog, int whichButton) 
								{
									XashActivity act = XashActivity.this;
									act.setFolderAsk( true );
									act.finish();
								}
							})
						.setCancelable(false)
						.show();
				}
				else
				{
					launchSurfaceAndEngine();
				}
			}*/
			
			mReturingWithResultCode = 0;
		}
	}
	
		// Events
	@Override
	protected void onPause() {
		Log.v( TAG, "onPause()" );
		
		if( mEngineReady )
		{
			// let engine save all configs before exiting.
			nativeOnPause();
		
			// wait until Xash will save all configs
			mSurface.engineThreadWait();
		}
		
		super.onPause();
	}

	@Override
	protected void onResume() {
		Log.v( TAG, "onResume()" );
		
		if( mEngineReady )
			nativeOnResume();
		
		mEnginePaused = false;
		
		super.onResume();
	}
	
	@Override
	protected void onStop() {
		Log.v( TAG, "onStop()" );
		/*if( mEngineReady )
		{
			nativeSetPause(0);
			// let engine properly exit, instead of killing it's thread
			nativeOnStop();
		
			// wait for engine
			mSurface.engineThreadWait();
		}*/
		super.onStop();
	}
	
	@Override
	protected void onDestroy() {
		Log.v( TAG, "onDestroy()" );
		
		if( mEngineReady )
		{
			nativeUnPause();
			
			// let engine a chance to properly exit
			nativeOnDestroy();
			
			//mSurface.engineThreadWait();
			
			// wait until Xash will exit
			mSurface.engineThreadJoin();
		}
		
		super.onDestroy();
	}
	
	@Override
	public void onWindowFocusChanged( boolean hasFocus ) 
	{
		if( mEngineReady )
		{
			nativeOnFocusChange();
		}
		
		super.onWindowFocusChanged( hasFocus );

		if( mImmersiveMode != null )
		{
			mImmersiveMode.apply();
		}
	}
	
	public static void setFolderAsk( Context ctx, Boolean b )
	{
		SharedPreferences pref = ctx.getSharedPreferences( "engine", 0 );
		
		if( pref.getBoolean( "folderask", true ) == b )
			return;
	
		SharedPreferences.Editor editor = pref.edit();
		
		editor.putBoolean( "folderask", b );
		editor.commit();
	}
	
	private void setNewBasedir( String baseDir )
	{
		SharedPreferences.Editor editor = mPref.edit();
		
		editor.putString( "basedir", baseDir );
		editor.commit();
	}
	
	private String getStringExtraFromIntent( Intent intent, String extraString, String ifNotFound )
	{
		String ret = intent.getStringExtra( extraString );
		if( ret == null )
		{
			ret = ifNotFound;
		}
		
		return ret;
	}
	
	private void checkWritePermission( String basedir )
	{
		Log.v( TAG, "Checking write permissions..." );

		if( nativeTestWritePermission( basedir ) == 0 )
		{
			Log.v( TAG, "First check has failed!" );
			
			String msg = null;
			
			if( sdk > 20 )
			{
				// 5.0 and higher _allows_ writing to SD card, but have broken fopen()/open() call. So, no Xash here. F*ck you, Google!
				msg = getString( R.string.lollipop_write_fail_msg );
			}
			else if( sdk > 18 )
			{
				// 4.4 and 4.4W does not allow SD card write at all
				msg = getString( R.string.kitkat_write_fail_msg );
			}
			else
			{
				// Read-only filesystem
				// Logically should be never reached
				msg = getString( R.string.readonly_fs_fail_msg );
			}
			
			new AlertDialog.Builder( this )
				.setTitle( R.string.write_failed )
				.setMessage( msg )
				.setPositiveButton( R.string.ok, new DialogInterface.OnClickListener() 
					{
						public void onClick( DialogInterface dialog, int whichButton ) 
						{
							XashActivity act = XashActivity.this;
							act.setFolderAsk( XashActivity.this, true );
							act.finish();
						}
					})
				.setCancelable( false )
				.show();
		}
		else
		{
			// everything is normal, so launch engine
			launchSurfaceAndEngine();
		}
	}
	
	private void launchSurfaceAndEngine()
	{
		Log.v( TAG, "Everything is OK. Launching engine..." );

		setupEnvironment();
		InstallReceiver.extractPAK( this, false );

		// Set up the surface
		mSurface = new EngineSurface( getApplication() );

		mLayout = new FrameLayout( this );
		mLayout.addView( mSurface );
		setContentView( mLayout );

		SurfaceHolder holder = mSurface.getHolder();
		holder.setType( SurfaceHolder.SURFACE_TYPE_GPU );

		if( sdk >= 14 )
			handler = new JoystickHandler_v14();
		else if( sdk >= 12 )
			handler = new JoystickHandler_v12();
		else 
			handler = new JoystickHandler();
		handler.init();
		
		mHasVibrator = mHasVibrator && ( handler.hasVibrator() );
		
		mPixelFormat = mPref.getInt( "pixelformat", 0 );
		mUseVolume = mPref.getBoolean( "usevolume", false );
		if( mPref.getBoolean( "enableResizeWorkaround", true ) )
			AndroidBug5497Workaround.assistActivity( this );
		
		// Immersive Mode is available only at >KitKat
		Boolean enableImmersive = ( sdk >= 19 ) && ( mPref.getBoolean( "immersive_mode", true ) );
		if( enableImmersive )
			mImmersiveMode = new ImmersiveMode_v19();
		else mImmersiveMode = new ImmersiveMode();
			
		mDecorView = getWindow().getDecorView();
		
		mVibrator = ( Vibrator )getSystemService( Context.VIBRATOR_SERVICE );

		if( mPref.getBoolean( "resolution_fixed", false ) )
		{
			if( mPref.getBoolean( "resolution_custom", false ) )
			{
				mForceWidth = mPref.getInt( "resolution_width", 854 );
				mForceHeight = mPref.getInt( "resolution_height", 480 );
				if( mForceWidth < mMinWidth || mForceHeight < mMinHeight )
					mForceWidth = mForceHeight = 0;
			}
			else
			{
				mScale = mPref.getFloat( "resolution_scale", 1 );
				if( mScale < 0.5 )
					mScale = 0;
				
				DisplayMetrics metrics = new DisplayMetrics();
				getWindowManager().getDefaultDisplay().getMetrics(metrics);
				
				if( (float)metrics.widthPixels / mScale < (float)mMinWidth || 
					(float)metrics.heightPixels / mScale < (float)mMinHeight )
				{
					mScale = 0;
				}
			}
		}

		if( sdk >= 5 )
			startService( new Intent( getBaseContext(), XashService.class ) );

		mEngineReady = true;
	}
	
	private void setupEnvironment()
	{
		Intent intent = getIntent();
		final String enginedir = getFilesDir().getParentFile().getPath() + "/lib";
				
		String argv       = getStringExtraFromIntent( intent, "argv", mPref.getString( "argv", "-dev 3 -log" ) );
		String gamelibdir = getStringExtraFromIntent( intent, "gamelibdir", enginedir );
		String gamedir    = getStringExtraFromIntent( intent, "gamedir", "valve" );
		String basedir    = getStringExtraFromIntent( intent, "basedir", mPref.getString( "basedir", "/sdcard/xash/" ) );
		String gdbsafe    = intent.getStringExtra( "gdbsafe" );
		
		bIsCstrike = ( gamedir.equals("cstrike") || gamedir.equals("czero") || gamedir.equals("czeror") );
		
		if( bIsCstrike )
		{
			mMinWidth = 640;
			mMinHeight = 480;
			
			final String allowed = "in.celest.xash3d.cs16client";
			
			if( !FWGSLib.checkGameLibDir( gamelibdir, allowed ) || 
				CertCheck.dumbCertificateCheck( getContext(), allowed, null, true ) )
			{
				finish();
				return;
			}
		}
		
		if( gdbsafe != null || Debug.isDebuggerConnected() )
		{
			fGDBSafe = true;
			Log.e( TAG, "GDBSafe mode enabled!" );
		}			
		
		mArgv = argv.split( " " );

		setenv( "XASH3D_BASEDIR",    basedir,    true );
		setenv( "XASH3D_ENGLIBDIR",  enginedir,  true );
		setenv( "XASH3D_GAMELIBDIR", gamelibdir, true );
		setenv( "XASH3D_GAMEDIR",    gamedir,    true );
		setenv( "XASH3D_EXTRAS_PAK1", getFilesDir().getPath() + "/extras.pak", true );
		
		String pakfile = intent.getStringExtra( "pakfile" );
		if( pakfile != null && pakfile != "" )
			setenv( "XASH3D_EXTRAS_PAK2", pakfile, true );
		
		String[] env = intent.getStringArrayExtra( "env" );
		if( env != null )
		{
			try
			{
				for( int i = 0; i + 1 < env.length; i += 2 )
				{
					setenv( env[i], env[i + 1], true );
				}
			}
			catch( Exception e )
			{
				e.printStackTrace();
			}
		}
	}
	
	public static native int  nativeInit( Object arguments );
	public static native void nativeQuit();
	public static native void onNativeResize( int x, int y );
	public static native void nativeTouch( int pointerFingerId, int action, float x, float y );
	public static native void nativeKey( int down, int code );
	public static native void nativeString( String text );
	public static native void nativeSetPause( int pause );
	public static native void nativeOnDestroy();
	public static native void nativeOnResume();
	public static native void nativeOnFocusChange();
	public static native void nativeOnPause();
	public static native void nativeUnPause();
	public static native void nativeHat( int id, byte hat, byte keycode, boolean down ) ;
	public static native void nativeAxis( int id, byte axis, short value );
	public static native void nativeJoyButton( int id, byte button, boolean down );
	public static native int  nativeTestWritePermission( String path );
	public static native void nativeMouseMove( float x, float y );
	
	// for future expansion
	public static native void nativeBall( int id, byte ball, short xrel, short yrel );
	public static native void nativeJoyAdd( int id );
	public static native void nativeJoyDel( int id );
	
	// libjnisetenv
	public static native int setenv( String key, String value, boolean overwrite );
	
	// Java functions called from C
	public static boolean createGLContext() 
	{
		return mSurface.InitGL();
	}
	
	public static void swapBuffers() 
	{
		mSurface.SwapBuffers();
	}
	
	public static void engineThreadNotify() 
	{
		mSurface.engineThreadNotify();
	}
	
	public static Surface getNativeSurface() 
	{
		return XashActivity.mSurface.getNativeSurface();
	}
	
	public static void vibrate( int time ) 
	{
		if( mHasVibrator )
		{
			mVibrator.vibrate( time );
		}
	}
	
	public static void toggleEGL( int toggle )
	{
		mSurface.toggleEGL( toggle );
	}
	
	public static boolean deleteGLContext() 
	{
		mSurface.ShutdownGL();
		return true;
	}

	public static Context getContext() 
	{
		return mSingleton;
	}
	
	protected final String[] messageboxData = new String[2];
	public static void messageBox( String title, String text )
	{
		mSingleton.messageboxData[0] = title;
		mSingleton.messageboxData[1] = text;
		mSingleton.runOnUiThread( new Runnable() 
		{
			@Override
			public void run()
			{
				new AlertDialog.Builder( mSingleton )
					.setTitle( mSingleton.messageboxData[0] )
					.setMessage( mSingleton.messageboxData[1] )
					.setPositiveButton( "Ok", new DialogInterface.OnClickListener() 
						{
							public void onClick( DialogInterface dialog, int whichButton ) 
							{
								synchronized( mSingleton.messageboxData )
								{
									mSingleton.messageboxData.notify();
								}
							}
						})
					.setCancelable( false )
					.show();
			}
		});
		synchronized( mSingleton.messageboxData ) 
		{
			try 
			{
				mSingleton.messageboxData.wait();
			} 
			catch( InterruptedException ex )
			{
				ex.printStackTrace();
			}
		}
	}

	public static boolean handleKey( int keyCode, KeyEvent event )
	{
		if( mUseVolume && ( keyCode == KeyEvent.KEYCODE_VOLUME_DOWN ||
			keyCode == KeyEvent.KEYCODE_VOLUME_UP ) )
			return false;
			
		final int source = XashActivity.handler.getSource( event );
		final int action = event.getAction();
		final boolean isGamePad  = ( source & InputDevice.SOURCE_GAMEPAD )        == InputDevice.SOURCE_GAMEPAD;
		final boolean isJoystick = ( source & InputDevice.SOURCE_CLASS_JOYSTICK ) == InputDevice.SOURCE_CLASS_JOYSTICK;
		final boolean isDPad     = ( source & InputDevice.SOURCE_DPAD )           == InputDevice.SOURCE_DPAD;
		
		if( isDPad )
		{
			byte val;
			final byte hat = 0;
			final int id = 0;
			Log.d( TAG, "DPAD button: " + keyCode );
			switch( keyCode )
			{
			case KeyEvent.KEYCODE_DPAD_CENTER: val = JOY_HAT_CENTERED; break;
			case KeyEvent.KEYCODE_DPAD_UP:     val = JOY_HAT_UP;       break;
			case KeyEvent.KEYCODE_DPAD_RIGHT:  val = JOY_HAT_RIGHT;    break;
			case KeyEvent.KEYCODE_DPAD_DOWN:   val = JOY_HAT_DOWN;     break;
			case KeyEvent.KEYCODE_DPAD_LEFT:   val = JOY_HAT_LEFT;     break;
			default: 
				return performEngineKeyEvent( action, keyCode, event );
			}

			if( action == KeyEvent.ACTION_DOWN )
			{
				nativeHat( id, hat, val, true );
			}
			else if( action == KeyEvent.ACTION_UP )
			{
				nativeHat( id, hat, val, false );
			}

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
					val = ( byte )( ( keyCode - KeyEvent.KEYCODE_BUTTON_1 ) + 15);
				}
				else if( XashActivity.handler.isGamepadButton( keyCode ) )
				{
					// maybe never reached, as all possible gamepad buttons are checked before
					Log.d( TAG, "Unhandled GamePad button: " + XashActivity.handler.keyCodeToString( keyCode ) );
					return false;
				}
				else
				{
					// must be never reached too
					return performEngineKeyEvent( action, keyCode, event );
				}
			}

			if( action == KeyEvent.ACTION_DOWN )
			{
				nativeJoyButton( id, val, true );
				return true;
			}
			else if( action == KeyEvent.ACTION_UP )
			{
				nativeJoyButton( id, val, false );
				return true;
			}
			return false;
		}

		return performEngineKeyEvent( action, keyCode, event );
	}

	public static boolean performEngineKeyEvent( int action, int keyCode, KeyEvent event )
	{
		//Log.v(TAG, "EngineKeyEvent( " + action +", " + keyCode +" "+ event.isCtrlPressed() +" )");
		if( action == KeyEvent.ACTION_DOWN )
		{
			if( event.isPrintingKey() || keyCode == 62 )// space is printing too
				XashActivity.nativeString( String.valueOf( ( char )event.getUnicodeChar() ) );
			
			XashActivity.nativeKey( 1, keyCode );
			
			return true;
		}
		/*else if( action == KeyEvent.ACTION_MULTIPLE )
		{
			if( keyCode == KeyEvent.KEYCODE_UNKNOWN )
			{
				XashActivity.nativeString( event.getCharacters() );
			}
			else
			{
				// maybe unneeded
			}
		}*/
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
			
			nativeAxis( id, engineAxis, ( short )( current * SHRT_MAX ) );
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

		public ShowTextInputTask( int show1 ) 
		{
		   show = show1;
		}

		@Override
		public void run() 
		{
			InputMethodManager imm = ( InputMethodManager )getContext().getSystemService( Context.INPUT_METHOD_SERVICE );
			
			if( mTextEdit == null )
			{
				mTextEdit = new DummyEdit( getContext() );
				mLayout.addView( mTextEdit );
			}
			if( show == 1 )
			{
				mTextEdit.setVisibility( View.VISIBLE );
				mTextEdit.requestFocus();
				
				imm.showSoftInput( mTextEdit, 0 );
				keyboardVisible = true;
				if( XashActivity.mImmersiveMode != null )
					XashActivity.mImmersiveMode.apply();
			}
			else
			{
				mTextEdit.setVisibility( View.GONE );
				imm.hideSoftInputFromWindow( mTextEdit.getWindowToken(), 0 );
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
		mSingleton.runOnUiThread( new ShowTextInputTask( show ) );
		//if( show == 1 )
		//	mSurface.getHolder().setSizeFromLayout();
	}

	public static void setIcon( String path )
	{
		if( fGDBSafe )
			return;
		
		Log.v( TAG, "setIcon(" + path + ")" );
		if( sdk < 5 )
			return;
		
		try
		{
			BitmapFactory.Options o = new BitmapFactory.Options();
			o.inJustDecodeBounds = true;
			
			Bitmap icon = BitmapFactory.decodeFile( path, o );
			
			if( icon.getWidth() < 16 )
				return;
			
			XashService.notification.contentView.setImageViewUri( XashService.status_image, Uri.parse( "file://" + path ) );
			
			NotificationManager nm = ( NotificationManager )mSingleton.getApplicationContext().getSystemService( Context.NOTIFICATION_SERVICE );
			nm.notify( 100, XashService.notification );
		}
		catch( Exception e )
		{
		}
	}
	
	public static void setTitle( String title )
	{
		Log.v( TAG, "setTitle(" + title + ")" );
		SharedPreferences.Editor editor = mPref.edit();
		editor.putBoolean("successfulRun", true);
		editor.commit();
		
		if( sdk < 5 )
			return;
		
		XashService.notification.contentView.setTextViewText( XashService.status_text, title );
		NotificationManager nm = ( NotificationManager )mSingleton.getApplicationContext().getSystemService( Context.NOTIFICATION_SERVICE );
		nm.notify( 100, XashService.notification );
	}

	public static String getAndroidID()
	{
		String str = Secure.getString( mSingleton.getContentResolver(), Secure.ANDROID_ID );
		
		if( str == null )
			return "";
		
		return str;
	}

	public static String loadID()
	{
		return mPref.getString( "xash_id", "" );
	}

	public static void saveID( String id )
	{
		SharedPreferences.Editor editor = mPref.edit();

		editor.putString( "xash_id", id );
		editor.commit();
	}

	public static void showMouse( int show )
	{
		fMouseShown = show != 0;
		handler.showMouse( fMouseShown );
	}
}


/**
 Simple nativeInit() runnable
 */
class XashMain implements Runnable 
{
	public void run()
	{
		XashActivity.nativeInit( XashActivity.mArgv );
	}
}

/**
 EngineSurface. This is what we draw on, so we need to know when it's created
 in order to do anything useful.

 Because of this, that's where we set up the Xash3D thread
 */
class EngineSurface extends SurfaceView implements SurfaceHolder.Callback, View.OnKeyListener 
{
	public static final String TAG = "XASH3D-EngineSurface";
	
	// This is what Xash3D runs in. It invokes main(), eventually
	private static Thread mEngThread = null;
	private static Object mPauseLock = new Object(); 

	// EGL private objects
	private EGLContext  mEGLContext;
	private EGLSurface  mEGLSurface;
	private EGLDisplay  mEGLDisplay;
	private EGL10 mEGL;
	private EGLConfig mEGLConfig;
	private boolean resizing = false;

	// Sensors

	// Startup
	public EngineSurface( Context context )
	{
		super( context );
		getHolder().addCallback( this );

		setFocusable( true );
		setFocusableInTouchMode( true );
		requestFocus();
		setOnKeyListener( this );
		if( XashActivity.sdk >= 5 )
			setOnTouchListener( new EngineTouchListener_v5() );
		else
			setOnTouchListener( new EngineTouchListener_v1() );
	}
	
	// Called when we have a valid drawing surface
	public void surfaceCreated( SurfaceHolder holder )
	{
		Log.v( TAG, "surfaceCreated()" );
		
		if( mEGL == null )
			return;
		
		XashActivity.nativeSetPause( 0 );
		XashActivity.mEnginePaused = false;
		//holder.setFixedSize(640,480);
		//SurfaceHolder.setFixedSize(640,480);
	}

	// Called when we lose the surface
	public void surfaceDestroyed( SurfaceHolder holder )
	{
		Log.v( TAG, "surfaceDestroyed()" );
		
		if( mEGL == null )
			return;
		
		XashActivity.nativeSetPause(1);
	}

	// Called when the surface is resized
	public void surfaceChanged( SurfaceHolder holder, int format, int width, int height )
	{
		Log.v( TAG, "surfaceChanged()" );
		if( ( XashActivity.mForceHeight!= 0 && XashActivity.mForceWidth!= 0 || XashActivity.mScale != 0 ) && !resizing )
		{
			int newWidth, newHeight;
			resizing = true;
			if( XashActivity.mForceHeight != 0 && XashActivity.mForceWidth != 0 )
			{
				newWidth = XashActivity.mForceWidth;
				newHeight = XashActivity.mForceHeight;
			}
			else
			{
				newWidth = ( int )( getWidth() / XashActivity.mScale );
				newHeight = ( int )( getHeight() / XashActivity.mScale );
			}
			holder.setFixedSize( newWidth, newHeight );
			XashActivity.mTouchScaleX = ( float )newWidth / getWidth();
			XashActivity.mTouchScaleY = ( float )newHeight / getHeight();
			
			return;
		}

		// Android may force only-landscape app to portait during lock
		// Just don't notify engine in that case
		if( width > height || mEngThread == null )
			XashActivity.onNativeResize( width, height );
		// holder.setFixedSize( width / 2, height / 2 );
		// Now start up the C app thread
		if( mEngThread == null ) 
		{
			mEngThread = new Thread( new XashMain(), "EngineThread" );
			mEngThread.start();
		}
		resizing = false;
	}
	
	public void engineThreadJoin()
	{
		Log.v( TAG, "engineThreadJoin()" );
		try
		{
			if( mEngThread != null )
				mEngThread.join( 5000 ); // wait until Xash will quit
		}
		catch( InterruptedException e )
		{
		}
	}
	
	public void engineThreadWait()
	{
		if( XashActivity.fGDBSafe )
			return;
		
		Log.v( TAG, "engineThreadWait()" );
		synchronized( mPauseLock )
		{
			try
			{
				mPauseLock.wait( 5000 ); // wait until engine notify
			}
			catch( InterruptedException e )
			{
			}
		}
	}
	
	public void engineThreadNotify()
	{
		if( XashActivity.fGDBSafe )
			return;
		
		Log.v( TAG, "engineThreadNotify()" );
		synchronized( mPauseLock )
		{
			mPauseLock.notify(); // send notify
		}
	}
	
	// unused
	public void onDraw( Canvas canvas )
	{
	}
	
	// first, initialize native backend
	public Surface getNativeSurface() 
	{
		return getHolder().getSurface();
	}
	
	// EGL functions
	public boolean InitGL() 
	{
		try
		{
			EGL10 egl = ( EGL10 )EGLContext.getEGL();
			
			if( egl == null )
			{
				Log.e( TAG, "Cannot get EGL from context" );
				return false;
			}

			EGLDisplay dpy = egl.eglGetDisplay( EGL10.EGL_DEFAULT_DISPLAY );

			if( dpy == null )
			{
				Log.e( TAG, "Cannot get display" );
				return false;
			}
			
			int[] version = new int[2];
			if( !egl.eglInitialize( dpy, version ) )
			{
				Log.e( TAG, "No EGL config available" );
				return false;
			}
			
			int[][] configSpec = 
			{
				{
					EGL10.EGL_DEPTH_SIZE, 8,
					EGL10.EGL_RED_SIZE,   8,
					EGL10.EGL_GREEN_SIZE, 8,
					EGL10.EGL_BLUE_SIZE,  8,
					EGL10.EGL_ALPHA_SIZE, 8,
					EGL10.EGL_NONE
				}, 
				{
					EGL10.EGL_DEPTH_SIZE, 8,
					EGL10.EGL_RED_SIZE,   8,
					EGL10.EGL_GREEN_SIZE, 8,
					EGL10.EGL_BLUE_SIZE,  8,
					EGL10.EGL_ALPHA_SIZE, 0,
					EGL10.EGL_NONE
				}, 
				{
					EGL10.EGL_DEPTH_SIZE, 8,
					EGL10.EGL_RED_SIZE,   5,
					EGL10.EGL_GREEN_SIZE, 6,
					EGL10.EGL_BLUE_SIZE,  5,
					EGL10.EGL_ALPHA_SIZE, 0,
					EGL10.EGL_NONE
				}, 
				{
					EGL10.EGL_DEPTH_SIZE, 8,
					EGL10.EGL_RED_SIZE,   5,
					EGL10.EGL_GREEN_SIZE, 5,
					EGL10.EGL_BLUE_SIZE,  5,
					EGL10.EGL_ALPHA_SIZE, 1,
					EGL10.EGL_NONE
				}, 
				{
					EGL10.EGL_DEPTH_SIZE, 8,
					EGL10.EGL_RED_SIZE,   4,
					EGL10.EGL_GREEN_SIZE, 4,
					EGL10.EGL_BLUE_SIZE,  4,
					EGL10.EGL_ALPHA_SIZE, 4,
					EGL10.EGL_NONE
				}, 
				{
					EGL10.EGL_DEPTH_SIZE, 8,
					EGL10.EGL_RED_SIZE,   3,
					EGL10.EGL_GREEN_SIZE, 3,
					EGL10.EGL_BLUE_SIZE,  2,
					EGL10.EGL_ALPHA_SIZE, 0,
					EGL10.EGL_NONE
				}
			};
			EGLConfig[] configs = new EGLConfig[1];
			int[] num_config = new int[1];
			if( !egl.eglChooseConfig( dpy, configSpec[XashActivity.mPixelFormat], configs, 1, num_config ) || num_config[0] == 0 )
			{
				Log.e( TAG, "No EGL config available" );
				return false;
			}
			EGLConfig config = configs[0];

			int EGL_CONTEXT_CLIENT_VERSION = 0x3098;
			int contextAttrs[] = new int[]
			{
				EGL_CONTEXT_CLIENT_VERSION, 1,
				EGL10.EGL_NONE
			};
			EGLContext ctx = egl.eglCreateContext( dpy, config, EGL10.EGL_NO_CONTEXT, contextAttrs );
			if( ctx == EGL10.EGL_NO_CONTEXT )
			{
				Log.e( TAG, "Couldn't create context" );
				return false;
			}

			EGLSurface surface = egl.eglCreateWindowSurface( dpy, config, this, null );
			if( surface == EGL10.EGL_NO_SURFACE )
			{
				Log.e( TAG, "Couldn't create surface" );
				return false;
			}

			if( !egl.eglMakeCurrent( dpy, surface, surface, ctx ) )
			{
				Log.e( TAG, "Couldn't make context current" );
				return false;
			}
			
			mEGLContext = ctx;
			mEGLDisplay = dpy;
			mEGLSurface = surface;
			mEGL = egl;
			mEGLConfig = config;
		} 
		catch( Exception e ) 
		{
			Log.v( TAG, e + "" );
			for( StackTraceElement s : e.getStackTrace() ) 
			{
				Log.v( TAG, s.toString() );
			}
		}
		
		return true;
	}

	// EGL buffer flip
	public void SwapBuffers()
	{
		if( mEGLSurface == null )
			return;
		
		mEGL.eglSwapBuffers( mEGLDisplay, mEGLSurface );
	}
	
	public void toggleEGL( int toggle )
	{
	   if( toggle != 0 )
	   {
			mEGLSurface = mEGL.eglCreateWindowSurface( mEGLDisplay, mEGLConfig, this, null );
			mEGL.eglMakeCurrent( mEGLDisplay, mEGLSurface, mEGLSurface, mEGLContext );
	   }
	   else
	   {
			mEGL.eglMakeCurrent( mEGLDisplay, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_CONTEXT );
			mEGL.eglDestroySurface( mEGLDisplay, mEGLSurface );
			mEGLSurface = null;
	   }
	}
	
	public void ShutdownGL()
	{
		mEGL.eglDestroyContext( mEGLDisplay, mEGLContext );
		mEGLContext = null;
		
		mEGL.eglDestroySurface( mEGLDisplay, mEGLSurface );
		mEGLSurface = null;
		
		mEGL.eglTerminate( mEGLDisplay );
		mEGLDisplay = null;
	}
	
	@Override
	public boolean onKey( View v, int keyCode, KeyEvent event )
	{
		return XashActivity.handleKey( keyCode, event );
	}

	@Override
	public boolean onKeyPreIme( int keyCode, KeyEvent event )
	{
		Log.v( TAG, "PreIme: " + keyCode );
		return super.dispatchKeyEvent( event );
	}

}

/* This is a fake invisible editor view that receives the input and defines the
 * pan&scan region
 */
class DummyEdit extends View implements View.OnKeyListener
{
	InputConnection ic;

	public DummyEdit( Context context )
	{
		super( context );
		setFocusableInTouchMode( true );
		setFocusable( true );
		setOnKeyListener( this );
	}

	@Override
	public boolean onCheckIsTextEditor()
	{
		return true;
	}

	@Override
	public boolean onKey( View v, int keyCode, KeyEvent event )
	{
		return XashActivity.handleKey( keyCode, event );
	}

	@Override
	public InputConnection onCreateInputConnection( EditorInfo outAttrs )
	{
		ic = new XashInputConnection( this, true );
		
		outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI
				| 33554432 /* API 11: EditorInfo.IME_FLAG_NO_FULLSCREEN */;
		
		return ic;
	}
}

class XashInputConnection extends BaseInputConnection 
{
	public XashInputConnection( View targetView, boolean fullEditor ) 
	{
		super( targetView, fullEditor );
	}

	@Override
	public boolean sendKeyEvent( KeyEvent event ) 
	{
		if( XashActivity.handleKey( event.getKeyCode(), event ) )
			return true;
		return super.sendKeyEvent( event );
	}

	@Override
	public boolean commitText( CharSequence text, int newCursorPosition )
	{
		// nativeCommitText(text.toString(), newCursorPosition);
		if( text.toString().equals( "\n" ) )
		{
			XashActivity.nativeKey( 1, KeyEvent.KEYCODE_ENTER );
			XashActivity.nativeKey( 0, KeyEvent.KEYCODE_ENTER );
		}
		XashActivity.nativeString( text.toString() );
		
		return super.commitText( text, newCursorPosition );
	}
	
	@Override
	public boolean setComposingText( CharSequence text, int newCursorPosition )
	{
		// a1batross:
		//  This method is intended to show composed text immediately
		//  that after will be replaced by text from "commitText" method
		//  Just leaving this unimplemented fixes "twice" input on T9/Swype-like keyboards
	
		//ativeSetComposingText(text.toString(), newCursorPosition);
		// XashActivity.nativeString( text.toString() );
		
		return super.setComposingText( text, newCursorPosition );
	}

	public native void nativeSetComposingText( String text, int newCursorPosition );

	@Override
	public boolean deleteSurroundingText( int beforeLength, int afterLength )
	{
		// Workaround to capture backspace key. Ref: http://stackoverflow.com/questions/14560344/android-backspace-in-webview-baseinputconnection
		// and https://bugzilla.libsdl.org/show_bug.cgi?id=2265
		if( beforeLength > 0 && afterLength == 0 ) 
		{
			boolean ret = true;
			// backspace(s)
			while( beforeLength-- > 0 ) 
			{
				boolean ret_key = sendKeyEvent( new KeyEvent( KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DEL ) )
								&& sendKeyEvent( new KeyEvent( KeyEvent.ACTION_UP, KeyEvent.KEYCODE_DEL ) );
				ret = ret && ret_key; 
			}
			return ret;
		}
		
		return super.deleteSurroundingText(beforeLength, afterLength);	
	}
}
class EngineTouchListener_v1 implements View.OnTouchListener
{
	// Touch events
	public boolean onTouch( View v, MotionEvent event )
	{
		XashActivity.nativeTouch( 0, event.getAction(), event.getX(), event.getY() );
		return true;
	}
}

class EngineTouchListener_v5 implements View.OnTouchListener
{
	float lx = 0, ly = 0;
	boolean secondarypressed = false;
	
	// Touch events
	public boolean onTouch( View v, MotionEvent event )
	{
		final int touchDevId   = event.getDeviceId();
		final int pointerCount = event.getPointerCount();
		int action = event.getActionMasked();
		int pointerFingerId, mouseButton, i = -1;
		float x, y;
		
		switch( action )
		{
			case MotionEvent.ACTION_MOVE:
				if( ( !XashActivity.fMouseShown ) && ( ( XashActivity.handler.getSource( event ) & InputDevice.SOURCE_MOUSE ) == InputDevice.SOURCE_MOUSE ) )
				{
					x = event.getX();
					y = event.getY();

					XashActivity.nativeMouseMove( x - lx, y - ly );
					lx = x;
					ly = y;
					return true;
				}
				
				for( i = 0; i < pointerCount; i++ )
				{
					pointerFingerId = event.getPointerId( i );
					x = event.getX( i ) * XashActivity.mTouchScaleX;
					y = event.getY( i ) * XashActivity.mTouchScaleY;
					XashActivity.nativeTouch( pointerFingerId, 2, x, y );
				}
				break;
			case MotionEvent.ACTION_UP:
			case MotionEvent.ACTION_DOWN:
				 if( !XashActivity.fMouseShown && ( ( XashActivity.handler.getSource( event ) & InputDevice.SOURCE_MOUSE ) == InputDevice.SOURCE_MOUSE ) )
				 {
					lx = event.getX();
					ly = event.getY();
					boolean down = ( action == MotionEvent.ACTION_DOWN ) || ( action == MotionEvent.ACTION_POINTER_DOWN );
					int buttonState = XashActivity.handler.getButtonState( event );
					if( down && ( buttonState & MotionEvent.BUTTON_SECONDARY ) != 0 )
					{
						XashActivity.nativeKey( 1, -243 );
						secondarypressed = true;
						return true;
					}
					else if( !down && secondarypressed && ( buttonState & MotionEvent.BUTTON_SECONDARY ) == 0 )
					{
						secondarypressed = false;
						XashActivity.nativeKey( 0, -243 );
						return true;
					}
					XashActivity.nativeKey( down ? 1 : 0, -241 );
					return true;
				}
				i = 0;
				// fallthrough
			case MotionEvent.ACTION_POINTER_UP:
			case MotionEvent.ACTION_POINTER_DOWN:
				// Non primary pointer up/down
				if( i == -1 ) 
				{
					i = event.getActionIndex();
				}
				
				pointerFingerId = event.getPointerId( i );
				
				x = event.getX( i ) * XashActivity.mTouchScaleX;
				y = event.getY( i ) * XashActivity.mTouchScaleY;
				if( action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP )
					XashActivity.nativeTouch( pointerFingerId,1, x, y );
				if( action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN )
					XashActivity.nativeTouch( pointerFingerId,0, x, y );
				break;
			case MotionEvent.ACTION_CANCEL:
				for( i = 0; i < pointerCount; i++ ) 
				{
					pointerFingerId = event.getPointerId( i );
					x = event.getX( i ) * XashActivity.mTouchScaleX;
					y = event.getY( i ) * XashActivity.mTouchScaleY;
					XashActivity.nativeTouch( pointerFingerId, 1, x, y );
				}
				break;
			default: break;
		}
		return true;
	}
}

class AndroidBug5497Workaround 
{
	// For more information, see https://code.google.com/p/android/issues/detail?id=5497
	// To use this class, simply invoke assistActivity() on an Activity that already has its content view set.

	public static void assistActivity ( Activity activity )
	{
		new AndroidBug5497Workaround( activity );
	}

	private View mChildOfContent;
	private int usableHeightPrevious;
	private FrameLayout.LayoutParams frameLayoutParams;

	private AndroidBug5497Workaround( Activity activity ) 
	{
		FrameLayout content = ( FrameLayout )activity.findViewById( android.R.id.content );
		mChildOfContent = content.getChildAt( 0 );
		mChildOfContent.getViewTreeObserver().addOnGlobalLayoutListener( new ViewTreeObserver.OnGlobalLayoutListener( ) 
		{
			public void onGlobalLayout() 
			{
				possiblyResizeChildOfContent();
			}
		});
		frameLayoutParams = ( FrameLayout.LayoutParams )mChildOfContent.getLayoutParams();
	}

	private void possiblyResizeChildOfContent() 
	{
		int usableHeightNow = computeUsableHeight();
		if( usableHeightNow != usableHeightPrevious ) 
		{
			int usableHeightSansKeyboard = mChildOfContent.getRootView().getHeight();
			int heightDifference = usableHeightSansKeyboard - usableHeightNow;
			if( heightDifference > ( usableHeightSansKeyboard / 4 ) ) 
			{
				// keyboard probably just became visible
				frameLayoutParams.height = usableHeightSansKeyboard - heightDifference;
				XashActivity.keyboardVisible = true;
			} 
			else 
			{
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
	
	private int computeUsableHeight() 
	{
		Rect r = new Rect();
		mChildOfContent.getWindowVisibleDisplayFrame( r );
		return r.bottom - r.top;
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
					0x00000100   // View.SYSTEM_UI_FLAG_LAYOUT_STABLE
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

class JoystickHandler
{
	public int getSource( KeyEvent event )
	{
		return InputDevice.SOURCE_UNKNOWN;
	}
	
	public int getSource( MotionEvent event )
	{
		return InputDevice.SOURCE_UNKNOWN;
	}
	
	public boolean handleAxis( MotionEvent event )
	{
		return false;
	}
	
	public boolean isGamepadButton( int keyCode )
	{
		return false;
	}
	
	public String keyCodeToString( int keyCode )
	{
		return String.valueOf( keyCode );
	}
	
	public void init()
	{
	}
	
	public boolean hasVibrator()
	{
		return true;
	}
	
	public void showMouse( boolean show )
	{
	}
	
	public int getButtonState( MotionEvent event )
	{
		return 0;
	}
}

class Wrap_NVMouseExtensions
{
	private static Object inputManager;
	private static Method mInputManager_setCursorVisibility;
	public static int nMotionEvent_AXIS_RELATIVE_X = 0;
	public static int nMotionEvent_AXIS_RELATIVE_Y = 0;
	
	//**************************************************************************
	static 
	{
		try 
		{
			mInputManager_setCursorVisibility =
				Class.forName( "android.hardware.input.InputManager" ).getMethod( "setCursorVisibility", boolean.class );
			
			inputManager = XashActivity.mSingleton.getSystemService( "input" );
		}
		catch( Exception ex ) 
		{ 
		}
		/* DO THE SAME FOR RELATIVEY */
	}
	
	//**************************************************************************
	public static void checkAvailable() throws Exception 
	{
		Field fieldMotionEvent_AXIS_RELATIVE_X = MotionEvent.class.getField( "AXIS_RELATIVE_X" );
		nMotionEvent_AXIS_RELATIVE_X = ( Integer )fieldMotionEvent_AXIS_RELATIVE_X.get( null );
		Field fieldMotionEvent_AXIS_RELATIVE_Y = MotionEvent.class.getField( "AXIS_RELATIVE_Y" );
		nMotionEvent_AXIS_RELATIVE_Y = ( Integer )fieldMotionEvent_AXIS_RELATIVE_Y.get( null );
	}
	
	//**************************************************************************
	public static void setCursorVisibility( boolean fVisibility ) 
	{
		try 
		{ 
			mInputManager_setCursorVisibility.invoke( inputManager, fVisibility ); 
		}
		catch( Exception e )
		{
		}
	}
	
	//**************************************************************************
	public static int getAxisRelativeX()
	{
		return nMotionEvent_AXIS_RELATIVE_X; 
	}
	
	public static int getAxisRelativeY() 
	{ 
		return nMotionEvent_AXIS_RELATIVE_Y; 
	}
}

class JoystickHandler_v12 extends JoystickHandler
{
	private static float prevSide, prevFwd, prevYaw, prevPtch, prevLT, prevRT, prevHX, prevHY;

	public static boolean mNVMouseExtensions = false;

	static 
	{
		try 
		{
			Wrap_NVMouseExtensions.checkAvailable();
			mNVMouseExtensions = true;
		}
		catch( Throwable t ) 
		{
			mNVMouseExtensions = false; 
		}
	}

	@Override
	public void init()
	{
		XashActivity.mSurface.setOnGenericMotionListener( new MotionListener() );
		Log.d( XashActivity.TAG, "mNVMouseExtensions = " + mNVMouseExtensions );
	}

	@Override
	public int getSource( KeyEvent event )
	{
		return event.getSource();
	}

	@Override
	public int getSource( MotionEvent event )
	{
		return event.getSource();
	}

	@Override
	public boolean handleAxis( MotionEvent event )
	{
		// how event can be from null device, Android?
		final InputDevice device = event.getDevice();
		if( device == null )
			return false;

		// maybe I need to cache this...
		for( InputDevice.MotionRange range: device.getMotionRanges() )
		{
			// normalize in -1.0..1.0 (copied from SDL2)
			final float cur = ( event.getAxisValue( range.getAxis(), event.getActionIndex() ) - range.getMin() ) / range.getRange() * 2.0f - 1.0f;
			final float dead = range.getFlat(); // get axis dead zone
			switch( range.getAxis() )
			{
			// typical axes
			// move
			case MotionEvent.AXIS_X:
				prevSide = XashActivity.performEngineAxisEvent( cur, XashActivity.JOY_AXIS_SIDE,  prevSide, dead );
				break;
			case MotionEvent.AXIS_Y:
				prevFwd  = XashActivity.performEngineAxisEvent( cur, XashActivity.JOY_AXIS_FWD,   prevFwd,  dead );
				break;

			// rotate. Invert, so by default this works as it's should
			case MotionEvent.AXIS_Z:
				prevPtch = XashActivity.performEngineAxisEvent( -cur, XashActivity.JOY_AXIS_PITCH, prevPtch, dead );
				break;
			case MotionEvent.AXIS_RZ:
				prevYaw  = XashActivity.performEngineAxisEvent( -cur, XashActivity.JOY_AXIS_YAW,   prevYaw,  dead );
				break;

			// trigger
			case MotionEvent.AXIS_RTRIGGER:
				prevLT = XashActivity.performEngineAxisEvent( cur, XashActivity.JOY_AXIS_RT, prevLT,   dead );
				break;
			case MotionEvent.AXIS_LTRIGGER:
				prevRT = XashActivity.performEngineAxisEvent( cur, XashActivity.JOY_AXIS_LT, prevRT,   dead );
				break;

			// hats
			case MotionEvent.AXIS_HAT_X:
				prevHX = XashActivity.performEngineHatEvent( cur, true, prevHX );
				break;
			case MotionEvent.AXIS_HAT_Y:
				prevHY = XashActivity.performEngineHatEvent( cur, false, prevHY );
				break;
			}
		}
		return true;
	}

	@Override
	public boolean isGamepadButton( int keyCode )
	{
		return KeyEvent.isGamepadButton( keyCode );
	}

	@Override
	public String keyCodeToString( int keyCode )
	{
		return KeyEvent.keyCodeToString( keyCode );
	}

	class MotionListener implements View.OnGenericMotionListener
	{
		@Override
		public boolean onGenericMotion( View view, MotionEvent event )
		{
			final int source = XashActivity.handler.getSource( event );
			final int axisDevices = InputDevice.SOURCE_CLASS_JOYSTICK | InputDevice.SOURCE_GAMEPAD;
			
			if( ( ( source & InputDevice.SOURCE_MOUSE ) == InputDevice.SOURCE_MOUSE ) && mNVMouseExtensions )
			{
				float x = event.getAxisValue( Wrap_NVMouseExtensions.getAxisRelativeX(), 0 );
				float y = event.getAxisValue( Wrap_NVMouseExtensions.getAxisRelativeY(), 0 );
				switch( event.getAction() ) 
				{
					case MotionEvent.ACTION_SCROLL:
					if( event.getAxisValue( MotionEvent.AXIS_VSCROLL ) < 0.0f )
					{
						XashActivity.nativeKey( 1, -239 );
						XashActivity.nativeKey( 0, -239 );
						return true;
					}
					else
					{
						XashActivity.nativeKey( 1, -240 );
						XashActivity.nativeKey( 0, -240 );
					}
					return true;
				}
				
				XashActivity.nativeMouseMove( x, y );
				//Log.v("XashInput", "MouseMove: " +x + " " + y );
				return true;
			}

			if( ( source & axisDevices ) != 0 )
				return XashActivity.handler.handleAxis( event );

			// TODO: Add it someday
			// else if( (event.getSource() & InputDevice.SOURCE_CLASS_TRACKBALL) == InputDevice.SOURCE_CLASS_TRACKBALL )
			//	return XashActivity.handleBall( event );
			//return super.onGenericMotion( view, event );
			return false;
		}
	}
	public boolean hasVibrator()
	{
		return XashActivity.mVibrator.hasVibrator();
	}
	
	public void showMouse( boolean show )
	{
		if( mNVMouseExtensions )
			Wrap_NVMouseExtensions.setCursorVisibility( show );
	}
}

class JoystickHandler_v14 extends JoystickHandler_v12
{
	public int getButtonState( MotionEvent event )
	{
		return event.getButtonState();
	}
}
