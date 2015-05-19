package com.beloko.touchcontrols;

import android.view.MotionEvent;

public class GenericAxisValues {
	float[] values = new float[64];

	public float getAxisValue(int a)
	{
		return values[a]; 
	}

	public void setAxisValue(int a,float v)
	{
		values[a] = v; 
	}

	public void setAndroidValues(MotionEvent event){
		for (int n=0;n<64;n++)
			values[n] = event.getAxisValue(n);
	}

	public void setMogaValues(com.bda.controller.MotionEvent event){
		values[MotionEvent.AXIS_X] =  event.getAxisValue(MotionEvent.AXIS_X);
		values[MotionEvent.AXIS_Y] =  event.getAxisValue(MotionEvent.AXIS_Y);
		values[MotionEvent.AXIS_Z] =  event.getAxisValue(MotionEvent.AXIS_Z);
		values[MotionEvent.AXIS_RZ] =  event.getAxisValue(MotionEvent.AXIS_RZ);
	}
}
