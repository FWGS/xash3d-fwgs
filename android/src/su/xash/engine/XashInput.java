


package su.xash.engine;
import java.lang.*;
import java.lang.reflect.*;
import java.util.*;
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
import android.view.inputmethod.*;
import su.xash.fwgslib.*;




public class XashInput
{
	public static JoystickHandler getJoystickHandler()
	{
		JoystickHandler handler;

		if( FWGSLib.sdk >= 14 )
			handler = new JoystickHandler_v14();
		else if( FWGSLib.sdk >= 12 )
			handler = new JoystickHandler_v12();
		else
			handler = new JoystickHandler();
		handler.init();
		return handler;
	}


public static class JoystickHandler
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


static class JoystickHandler_v12 extends JoystickHandler
{
	private static float prevSide, prevFwd, prevYaw, prevPtch, prevLT, prevRT, prevHX, prevHY;

	public static boolean mNVMouseExtensions = false;

	static int mouseId;
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
		if( event.getDeviceId() == mouseId )
			return InputDevice.SOURCE_MOUSE;
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
			
			if( FWGSLib.FExactBitSet( source, InputDevice.SOURCE_GAMEPAD ) || 
				FWGSLib.FExactBitSet( source, InputDevice.SOURCE_CLASS_JOYSTICK ) )
				return XashActivity.handler.handleAxis( event );

			if( mNVMouseExtensions )
			{
				float x = event.getAxisValue( Wrap_NVMouseExtensions.getAxisRelativeX(), 0 );
				float y = event.getAxisValue( Wrap_NVMouseExtensions.getAxisRelativeY(), 0 );
				if( !FWGSLib.FExactBitSet( source, InputDevice.SOURCE_MOUSE) && (x != 0 || y != 0 ))
					mouseId = event.getDeviceId();
				
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
				// Log.v("XashInput", "MouseMove: " +x + " " + y );
				return true;
			}

			// TODO: Add it someday
			// else if( (event.getSource() & InputDevice.SOURCE_CLASS_TRACKBALL) == InputDevice.SOURCE_CLASS_TRACKBALL )
			//	return XashActivity.handleBall( event );
			//return super.onGenericMotion( view, event );
			return false;
		}
	}
	
	@Override
	public boolean hasVibrator()
	{
		if( XashActivity.mVibrator != null )
			return XashActivity.mVibrator.hasVibrator();
		return false;
	}
	
	@Override
	public void showMouse( boolean show )
	{
		if( mNVMouseExtensions )
			Wrap_NVMouseExtensions.setCursorVisibility( show );
	}
}

static class JoystickHandler_v14 extends JoystickHandler_v12
{
	@Override
	public int getButtonState( MotionEvent event )
	{
		return event.getButtonState();
	}
}

	public static View.OnTouchListener getTouchListener()
	{
		if( FWGSLib.sdk >= 5 )
			return new EngineTouchListener_v5();
		else
			return new EngineTouchListener_v1();
	}

}

class Wrap_NVMouseExtensions
{
	private static Object inputManager;
	private static Method mInputManager_setCursorVisibility;
	private static Method mView_setPointerIcon;
	private static Class mPointerIcon;
	private static Object mEmptyIcon;
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
			try
			{
				mPointerIcon=Class.forName("android.view.PointerIcon");
				mEmptyIcon = mPointerIcon.getDeclaredMethod("getSystemIcon",android.content.Context.class, int.class).invoke(null,XashActivity.mSingleton.getContext(),0);
				mView_setPointerIcon = View.class.getMethod("setPointerIcon",mPointerIcon);
			}
			catch( Exception ex1 )
			{
				ex1.printStackTrace();
			}
		}
		/* DO THE SAME FOR RELATIVEY */
	}
	
	//*************************************************************************
	public static void checkAvailable() throws Exception 
	{
		Field fieldMotionEvent_AXIS_RELATIVE_X = MotionEvent.class.getField( "AXIS_RELATIVE_X" );
		nMotionEvent_AXIS_RELATIVE_X = ( Integer )fieldMotionEvent_AXIS_RELATIVE_X.get( null );
		Field fieldMotionEvent_AXIS_RELATIVE_Y = MotionEvent.class.getField( "AXIS_RELATIVE_Y" );
		nMotionEvent_AXIS_RELATIVE_Y = ( Integer )fieldMotionEvent_AXIS_RELATIVE_Y.get( null );
	}
	
	static void setPointerIcon( View view, boolean fVisibility )
	{
		Log.v("XashInput", "SET CURSOR VISIBILITY " + fVisibility + " obj " + mEmptyIcon.toString() );
		try
		{
			mView_setPointerIcon.invoke(view,fVisibility?null:mEmptyIcon);
		}
		catch( Exception e)
		{
			e.printStackTrace();
		}
	}
	static void setGroupPointerIcon( ViewGroup parent, boolean fVisibility )
	{
		for( int i = parent.getChildCount() - 1; i >= 0; i-- ) 
		{
			try
			{
				final View child = parent.getChildAt(i);

				if( child == null )
					continue;

				if( child instanceof ViewGroup )
				{
					setGroupPointerIcon((ViewGroup) child, fVisibility);
				} 
				setPointerIcon( child, fVisibility);
			
			}
			catch( Exception ex )
			{
				ex.printStackTrace();
			}
		}
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
			try
			{
				ViewGroup rootViewGroup = (ViewGroup) XashActivity.mSingleton.getWindow().getDecorView();
				setGroupPointerIcon(rootViewGroup, fVisibility);
				setGroupPointerIcon((ViewGroup)XashActivity.mDecorView, fVisibility);
				for (int i = 0; i < rootViewGroup.getChildCount(); i++) {
					View view = rootViewGroup.getChildAt(i);
					setPointerIcon(view, fVisibility);
				}
				}
				catch( Exception ex)
				{
					ex.printStackTrace();
				}
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
		//final int touchDevId   = event.getDeviceId();
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
