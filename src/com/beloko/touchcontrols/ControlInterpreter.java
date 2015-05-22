package com.beloko.touchcontrols;

import java.io.IOException;
import java.util.HashMap;

import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;

import com.bda.controller.Controller;
import com.beloko.touchcontrols.ControlConfig.Type;
import com.beloko.touchcontrols.Settings.IDGame;

public class ControlInterpreter {

	String LOG = "QuakeControlInterpreter";

	ControlInterface quakeIf;
	ControlConfig config;

	boolean gamePadEnabled;

	float screenWidth, screenHeight;

	HashMap<Integer, Boolean> analogButtonState = new HashMap<Integer, Boolean>(); //Saves current state of analog buttons so all sent each time

	public ControlInterpreter(ControlInterface qif,IDGame game,String controlfile,boolean ctrlEn)
	{
		Log.d("QuakeControlInterpreter", "file = " + controlfile);

		gamePadEnabled = ctrlEn;

		config = new ControlConfig(controlfile,game);
		try {
			config.loadControls();
		} catch (IOException e) {
			// TODO Auto-generated catch block
			//e.printStackTrace();
		} catch (ClassNotFoundException e) {
			// TODO Auto-generated catch block
			//e.printStackTrace();
		}

		for (ActionInput ai: config.actions)
		{
			if ((ai.sourceType == Type.ANALOG) && ((ai.actionType == Type.MENU) || (ai.actionType == Type.BUTTON)))
			{
				analogButtonState.put(ai.actionCode, false);
			}
		}

		quakeIf = qif;
	}

	public void setScreenSize(int w,int h)
	{
		screenWidth = w;
		screenHeight = h;
	}

	public boolean onTouchEvent(MotionEvent event)
	{
		int action = event.getAction();
		int actionCode = action & MotionEvent.ACTION_MASK;

		if (actionCode == MotionEvent.ACTION_MOVE)
		{

			for (int i = 0; i < event.getPointerCount(); i++) {

				float x = event.getX(i)/screenWidth;
				float y = event.getY(i)/screenHeight;
				int pid = event.getPointerId(i);
				quakeIf.touchEvent_if(3, pid, x, y);
			}
		}
		else if (actionCode == MotionEvent.ACTION_DOWN)
		{
			float x = event.getX()/screenWidth;
			float y = event.getY()/screenHeight;
			quakeIf.touchEvent_if(1, 0, x, y);
		}
		else if (actionCode == MotionEvent.ACTION_POINTER_DOWN)
		{
			int index = event.getActionIndex();
			if (index != -1)
			{
				float x = event.getX(index)/screenWidth;
				float y = event.getY(index)/screenHeight;
				int pid = event.getPointerId(index);
				quakeIf.touchEvent_if(1, pid, x, y); 
			}
		}
		else if (actionCode == MotionEvent.ACTION_POINTER_UP)
		{
			int index = event.getActionIndex();
			if (index != -1)
			{

				float x = event.getX(index)/screenWidth;
				float y = event.getY(index)/screenHeight;
				int pid = event.getPointerId(index);
				quakeIf.touchEvent_if(2, pid, x, y);
			}
		}
		else if (actionCode == MotionEvent.ACTION_UP)
		{
			float x = event.getX()/screenWidth;
			float y = event.getY()/screenHeight;
			int index = event.getActionIndex();
			int pid = event.getPointerId(index);

			quakeIf.touchEvent_if(2, pid, x, y);
		}

		return true;
	}


	public void onMogaKeyEvent(com.bda.controller.KeyEvent event,int pad_version)
	{
		int keycode =  event.getKeyCode();

		if (pad_version ==  Controller.ACTION_VERSION_MOGA)
		{
			//Log.d(LOG,"removed");
			if ((keycode == com.bda.controller.KeyEvent.KEYCODE_DPAD_DOWN) || 
					(keycode == com.bda.controller.KeyEvent.KEYCODE_DPAD_UP) || 
					(keycode == com.bda.controller.KeyEvent.KEYCODE_DPAD_LEFT) || 
					(keycode == com.bda.controller.KeyEvent.KEYCODE_DPAD_RIGHT))
				return;
		}	

		if (event.getAction() == com.bda.controller.KeyEvent.ACTION_DOWN)
			onKeyDown(keycode, null);
		else if (event.getAction() == com.bda.controller.KeyEvent.ACTION_UP)
			onKeyUp(keycode, null);
	}

	public boolean onKeyDown(int keyCode, KeyEvent event)
	{
		boolean used = false;;	
		if (gamePadEnabled)
		{
			for (ActionInput ai: config.actions)
			{
				if (((ai.sourceType == Type.BUTTON)||(ai.sourceType == Type.MENU)) && (ai.source == keyCode))
				{
					quakeIf.doAction_if(1, ai.actionCode);
					Log.d(LOG,"key down intercept");
					used =  true;
				}
			}
		}

		if (used)
			return true;


		if ((keyCode == KeyEvent.KEYCODE_VOLUME_UP) || //If these were mapped it would have already returned
				(keyCode == KeyEvent.KEYCODE_VOLUME_DOWN))
			return false;
		else
		{
			int uc = 0;
			if (event !=null)
				uc = event.getUnicodeChar();
			quakeIf.keyPress_if(1, quakeIf.mapKey(keyCode, uc), uc);
			return true;
		}
	}

	public boolean onKeyUp(int keyCode, KeyEvent event)
	{
		boolean used = false;

		if (gamePadEnabled)
		{
			for (ActionInput ai: config.actions)
			{
				if (((ai.sourceType == Type.BUTTON) || (ai.sourceType == Type.MENU)) && (ai.source == keyCode))
				{
					quakeIf.doAction_if(0, ai.actionCode);
					used = true;
				}
			}
		}

		if (used)
			return true;

		if ((keyCode == KeyEvent.KEYCODE_VOLUME_UP) || //If these were mapped it would have already returned
				(keyCode == KeyEvent.KEYCODE_VOLUME_DOWN))
			return false;
		else
		{
			int uc = 0;
			if (event !=null)
				uc = event.getUnicodeChar();
			quakeIf.keyPress_if(0, quakeIf.mapKey(keyCode, uc), uc);
			return true;
		}

	}

	float deadRegion = 0.2f;
	private float analogCalibrate(float v)
	{
		if ((v < deadRegion) && (v > -deadRegion))
			return 0;
		else
		{
			if (v > 0)
				return(v-deadRegion) / (1-deadRegion);
			else
				return(v+deadRegion) / (1-deadRegion);
			//return v;
		}
	}

	GenericAxisValues genericAxisValues = new GenericAxisValues();


	//This is for normal Android motioon event
	public boolean onGenericMotionEvent(MotionEvent event) {
		genericAxisValues.setAndroidValues(event);
		return onGenericMotionEvent(genericAxisValues);
	}

	//This is for Moga event
	public boolean onGenericMotionEvent(com.bda.controller.MotionEvent event) {
		genericAxisValues.setMogaValues(event);
		return onGenericMotionEvent(genericAxisValues);
	}

	public boolean onGenericMotionEvent(GenericAxisValues event) {
		if (Settings.DEBUG) Log.d(LOG,"onGenericMotionEvent" );

		boolean used = false;
		if (gamePadEnabled)
		{
			for (ActionInput ai: config.actions)
			{
				if ((ai.sourceType == Type.ANALOG) && (ai.source != -1))
				{
					int invert;
					invert = ai.invert?-1:1;
					if (ai.actionCode == ControlConfig.ACTION_ANALOG_PITCH)
						quakeIf.analogPitch_if(ControlConfig.LOOK_MODE_JOYSTICK, analogCalibrate(event.getAxisValue(ai.source)) * invert * ai.scale);
					else if (ai.actionCode == ControlConfig.ACTION_ANALOG_YAW)
						quakeIf.analogYaw_if(ControlConfig.LOOK_MODE_JOYSTICK, -analogCalibrate(event.getAxisValue(ai.source)) * invert * ai.scale);
					else if (ai.actionCode == ControlConfig.ACTION_ANALOG_FWD)
						quakeIf.analogFwd_if(-analogCalibrate(event.getAxisValue(ai.source)) * invert * ai.scale);
					else if (ai.actionCode == ControlConfig.ACTION_ANALOG_STRAFE)
						quakeIf.analogSide_if(analogCalibrate(event.getAxisValue(ai.source)) * invert * ai.scale);
					else //Must be using analog as a button
					{  
						if (Settings.DEBUG) Log.d(LOG,"Analog as button" );

						if (Settings.DEBUG) Log.d(LOG,ai.toString());

						if (((ai.sourcePositive) && (event.getAxisValue(ai.source)) > 0.5) ||
								((!ai.sourcePositive) && (event.getAxisValue(ai.source)) < -0.5) )
						{
							if (!analogButtonState.get(ai.actionCode)) //Check internal state, only send if different
							{
								quakeIf.doAction_if(1, ai.actionCode); //press
								analogButtonState.put(ai.actionCode, true);
							}
						}
						else
						{
							if (analogButtonState.get(ai.actionCode)) //Check internal state, only send if different
							{
								quakeIf.doAction_if(0, ai.actionCode); //un-press
								analogButtonState.put(ai.actionCode, false);
							}
						}

					}
					used = true;
				}
				/*
				//Menu buttons
				if ((ai.sourceType == Type.ANALOG) && (ai.actionType == Type.MENU) && (ai.source != -1))
				{
					if (GD.DEBUG) Log.d(LOG,"Analog as MENU button" );
					if (GD.DEBUG) Log.d(LOG,ai.toString());
					if (((ai.sourcePositive) && (event.getAxisValue(ai.source)) > 0.5) ||
							((!ai.sourcePositive) && (event.getAxisValue(ai.source)) < -0.5) )
						quakeIf.doAction_if(1, ai.actionCode); //press
					else
						quakeIf.doAction_if(0, ai.actionCode); //un-press
				}
				 */
			}

		}


		return used;

	}
}
