package com.beloko.games.hl;

import com.beloko.touchcontrols.ControlInterface;

public class NativeLib implements ControlInterface{


	public static native int init(String graphics_dir,int mem,String[] args,int game,String path);
	public static native int initTouchControls(String graphics_dir,int width,int height);

	public static native void setScreenSize( int width, int height );

	public static native boolean touchEvent( int action, int pid, float x, float y);
	public static native void keypress(int down, int qkey, int unicode);
	public static native void doAction(int state, int action);
	public static native void analogFwd(float v);
	public static native void analogSide(float v);
	public static native void analogPitch(int mode,float v);
	public static native void analogYaw(int mode,float v);
	public static native void setTouchSettings(float alpha,float strafe,float fwd,float pitch,float yaw,int other);

	public static native void quickCommand(String command);

	@Override
	public void initTouchControls_if(String pngPath,int width,int height) {
		initTouchControls(pngPath,width,height);
	}
	
	@Override
	public void quickCommand_if(String command){
		quickCommand(command);
	}

	@Override
	public boolean touchEvent_if(int action, int pid, float x, float y) {
		return touchEvent(  action,  pid,  x,  y);
	}
	@Override
	public void keyPress_if(int down, int qkey, int unicode) {
		keypress(down,qkey,unicode);
	}
	
	@Override
	public void doAction_if(int state, int action) {
		doAction(state,action);
	} 

	@Override
	public void analogFwd_if(float v) {
		analogFwd(v);
	}
	@Override
	public void analogSide_if(float v) {
		analogSide(v);
	}
	@Override
	public void  analogPitch_if(int mode,float v)
	{
		analogPitch(mode,v);
	}
	@Override
	public void  analogYaw_if(int mode,float v)
	{
		analogYaw(mode,v);
	}

	@Override
	public void setTouchSettings_if(float alpha,float strafe, float fwd, float pitch,
			float yaw, int other) {
		setTouchSettings(alpha,strafe, fwd, pitch, yaw, other);

	}

	@Override
	public int mapKey(int acode, int unicode) {
		// TODO Auto-generated method stub
		return 0;
	}

	
}
