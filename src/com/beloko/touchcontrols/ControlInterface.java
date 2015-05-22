package com.beloko.touchcontrols;

import android.view.KeyEvent;

public interface ControlInterface {
	
	public void initTouchControls_if(String pngPath,int width,int height);
	
	public boolean touchEvent_if( int action, int pid, float x, float y);
	public void    keyPress_if(int down, int qkey, int unicode);
	public void    doAction_if(int state, int action);
	public void    analogFwd_if(float v);
	public void    analogSide_if(float v);
	public void    analogPitch_if(int mode,float v);
	public void    analogYaw_if(int mode,float v);
	public void    setTouchSettings_if(float alpha,float strafe,float fwd,float pitch,float yaw,int other);
	
	public void quickCommand_if(String command);
	
	public  int    mapKey(int acode,int unicode);
}
 