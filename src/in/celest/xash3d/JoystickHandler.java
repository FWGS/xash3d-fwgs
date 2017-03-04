package in.celest.xash3d;

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

import java.lang.*;
import java.util.List;

import in.celest.xash3d.hl.BuildConfig;
import in.celest.xash3d.XashConfig;
import in.celest.xash3d.XashActivity;


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
	
	@Override
	public void init()
	{
		XashActivity.mSurface.setOnGenericMotionListener(new MotionListener());
	}
	
	@Override
	public int getSource(KeyEvent event) 
	{
		return event.getSource();
	}
	
	@Override
	public int getSource(MotionEvent event)
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
				prevSide = XashActivity.performEngineAxisEvent(cur, XashActivity.JOY_AXIS_SIDE,  prevSide, dead); 
				break;
			case MotionEvent.AXIS_Y: 
				prevFwd  = XashActivity.performEngineAxisEvent(cur, XashActivity.JOY_AXIS_FWD,   prevFwd,  dead); 
				break;
			
			// rotate. Invert, so by default this works as it's should
			case MotionEvent.AXIS_Z: 
				prevPtch = XashActivity.performEngineAxisEvent(-cur, XashActivity.JOY_AXIS_PITCH, prevPtch, dead); 
				break;
			case MotionEvent.AXIS_RZ: 
				prevYaw  = XashActivity.performEngineAxisEvent(-cur, XashActivity.JOY_AXIS_YAW,   prevYaw,  dead); 
				break;
			
			// trigger
			case MotionEvent.AXIS_RTRIGGER:	
				prevLT = XashActivity.performEngineAxisEvent(cur, XashActivity.JOY_AXIS_RT, prevLT,   dead); 
				break;
			case MotionEvent.AXIS_LTRIGGER:	
				prevRT = XashActivity.performEngineAxisEvent(cur, XashActivity.JOY_AXIS_LT, prevRT,   dead); 
				break;
			
			// hats
			case MotionEvent.AXIS_HAT_X: 
				prevHX = XashActivity.performEngineHatEvent(cur, true, prevHX); 
				break;
			case MotionEvent.AXIS_HAT_Y: 
				prevHY = XashActivity.performEngineHatEvent(cur, false, prevHY); 
				break;
			}
		}

		return true;
	}
	
	@Override
	public boolean isGamepadButton(int keyCode)
	{
		return KeyEvent.isGamepadButton(keyCode);
	}
	
	@Override
	public String keyCodeToString(int keyCode)
	{
		return KeyEvent.keyCodeToString(keyCode);
	}

	class MotionListener implements View.OnGenericMotionListener
	{
		@Override
		public boolean onGenericMotion(View view, MotionEvent event)
		{
			final int source = XashActivity.handler.getSource(event);
			final int axisDevices = InputDevice.SOURCE_CLASS_JOYSTICK | InputDevice.SOURCE_GAMEPAD;
			if( (source & axisDevices) != 0 )
				return XashActivity.handler.handleAxis( event );
			// TODO: Add it someday
			// else if( (event.getSource() & InputDevice.SOURCE_CLASS_TRACKBALL) == InputDevice.SOURCE_CLASS_TRACKBALL )
			//	return XashActivity.handleBall( event );
			//return super.onGenericMotion( view, event );
			return false;
		}
	}
}
 
