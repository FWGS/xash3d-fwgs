/*
 * Copyright (C) 2009 jeyries@yahoo.fr
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.beloko.games.hl;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

import org.libsdl.app.SDLActivity;

import android.app.Activity;
import android.content.Context;
import android.graphics.PixelFormat;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Window;
import android.view.WindowManager;

import com.bda.controller.Controller;
import com.bda.controller.ControllerListener;
import com.bda.controller.StateEvent;
import com.beloko.idtech.AppSettings;
import com.beloko.idtech.BestEglChooser;
import com.beloko.idtech.CDAudioPlayer;
import com.beloko.idtech.MyGLSurfaceView;
import com.beloko.idtech.Utils;
import com.beloko.libsdl.SDLLib;
import com.beloko.touchcontrols.FPSLimit;
import com.beloko.touchcontrols.ControlInterpreter;
import com.beloko.touchcontrols.Settings;
import com.beloko.touchcontrols.QuakeCustomCommands;
import com.beloko.touchcontrols.TouchControlsSettings;
import com.beloko.touchcontrols.Settings.IDGame;

public class Game extends Activity 
implements Handler.Callback
{
	String LOG = "Quake2";

	private ControlInterpreter controlInterp;

	private final MogaControllerListener mogaListener = new MogaControllerListener();
	Controller mogaController = null;

	private String args;
	private String gamePath;

	private GameView mGLSurfaceView = null;
	private QuakeRenderer mRenderer = new QuakeRenderer();
	Activity act;

	int surfaceWidth,surfaceHeight;

	int useGL;

	/** Called when the activity is first created. */
	@Override
	public void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);           

		act = this;

		AppSettings.setGame(IDGame.Wolf3d);
		AppSettings.reloadSettings(getApplication());

		args = getIntent().getStringExtra("args");
		gamePath  = getIntent().getStringExtra("game_path");
		useGL  = getIntent().getIntExtra("use_gl", 0);

		handlerUI  = new Handler(this);         

		mogaController = Controller.getInstance(this);
		mogaController.init();
		mogaController.setListener(mogaListener,new Handler());

		//Log.i( "Quake2", "version : " + getVersion());

		// fullscreen
		requestWindowFeature(Window.FEATURE_NO_TITLE);
		getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
				WindowManager.LayoutParams.FLAG_FULLSCREEN);

		// keep screen on 
		getWindow().setFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON,
				WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);



		start_quake2();   

	}


	/// Handler for asynchronous message
	/// => showDialog

	private Handler handlerUI ;

	public static final int MSG_SHOW_DIALOG = 1;


	// implements Handler.Callback
	@Override
	public boolean handleMessage(Message msg) {

		Log.i( "Quake2", String.format("handleMessage %d %d", msg.what, msg.arg1));

		switch( msg.what ){

		case MSG_SHOW_DIALOG:
			showDialog(msg.arg1);
			break;

		}

		return true;

	}


	/////////////////////////////




	public void start_quake2() {

		NativeLib.loadLibraries(useGL == 1);


		NativeLib engine = new NativeLib();

		NativeLib.loadLibraries(false);

		controlInterp = new ControlInterpreter(engine,Settings.IDGame.Doom,AppSettings.gamePadControlsFile,AppSettings.gamePadEnabled);

		TouchControlsSettings.setup(act, engine);
		TouchControlsSettings.loadSettings(act);
		TouchControlsSettings.sendToQuake();

		QuakeCustomCommands.setup(act, engine,getIntent().getStringExtra("main_qc"),getIntent().getStringExtra("mod_qc"));


		// Create our Preview view and set it as the content of our
		// Activity
		mGLSurfaceView = new GameView(this);

		NativeLib.gv = mGLSurfaceView;

		//if (renderType == NativeLib.REND_SOFT) //SDL software mode uses gles2
		if (useGL == 1)
			mGLSurfaceView.setEGLContextClientVersion(2); // enable OpenGL 2.0



		//mGLSurfaceView.setGLWrapper( new MyWrapper());
		//mGLSurfaceView.setDebugFlags(GLSurfaceView.DEBUG_CHECK_GL_ERROR | GLSurfaceView.DEBUG_LOG_GL_CALLS);
		//setEGLConfigChooser  (int redSize, int greenSize, int blueSize, int alphaSize, int depthSize, int stencilSize)
		//mGLSurfaceView.setEGLConfigChooser(8,8,8,0,16,0);
		mGLSurfaceView.setEGLConfigChooser( new BestEglChooser(getApplicationContext()) );

		mGLSurfaceView.setRenderer(mRenderer);

		// This will keep the screen on, while your view is visible. 
		mGLSurfaceView.setKeepScreenOn(true);

		setContentView(mGLSurfaceView);
		mGLSurfaceView.requestFocus();
		mGLSurfaceView.setFocusableInTouchMode(true);

	}



	@Override
	protected void onPause() {
		Log.i( "Quake2.java", "onPause" );
		CDAudioPlayer.onPause();
		SDLLib.onPause();
		mogaController.onPause();
		super.onPause();
	}

	@Override
	protected void onResume() {

		Log.i( "Quake2.java", "onResume" );
		CDAudioPlayer.onResume();
		SDLLib.onResume();
		mogaController.onResume();

		super.onResume();
		mGLSurfaceView.onResume();

	}

	@Override
	protected void onDestroy() {
		Log.i( "Quake2.java", "onDestroy" ); 
		super.onDestroy();
		mogaController.exit();
		System.exit(0);
	}

	class MogaControllerListener implements ControllerListener {


		@Override
		public void onKeyEvent(com.bda.controller.KeyEvent event) {
			//Log.d(LOG,"onKeyEvent " + event.getKeyCode());
			controlInterp.onMogaKeyEvent(event,mogaController.getState(Controller.STATE_CURRENT_PRODUCT_VERSION));
		}

		@Override
		public void onMotionEvent(com.bda.controller.MotionEvent event) {
			// TODO Auto-generated method stub
			Log.d(LOG,"onGenericMotionEvent " + event.toString());
			controlInterp.onGenericMotionEvent(event);
		}

		@Override
		public void onStateEvent(StateEvent event) {
			Log.d(LOG,"onStateEvent " + event.getState());
		}
	}

	class GameView extends MyGLSurfaceView {

		/*--------------------
		 * Event handling
		 *--------------------*/


		public GameView(Context context) {
			super(context);

		}

		@Override
		public boolean onGenericMotionEvent(MotionEvent event) {
			return controlInterp.onGenericMotionEvent(event);
		}
		@Override
		public boolean onTouchEvent(MotionEvent event)
		{
			return controlInterp.onTouchEvent(event);
		}

		@Override
		public boolean onKeyDown(int keyCode, KeyEvent event)
		{
			return controlInterp.onKeyDown(keyCode, event);
		}

		@Override
		public boolean onKeyUp(int keyCode, KeyEvent event)
		{
			return controlInterp.onKeyUp(keyCode, event);
		} 

	}  // end of QuakeView




	///////////// GLSurfaceView.Renderer implementation ///////////

	class QuakeRenderer implements MyGLSurfaceView.Renderer {



		public void onSurfaceCreated(GL10 gl, EGLConfig config) {
			Log.d("Renderer", "onSurfaceCreated");
		}


		private void init( int width, int height ){

			Log.i( "Quake2", "screen size : " + width + "x"+ height);

			NativeLib.setScreenSize(width,height);

			Utils.copyPNGAssets(getApplicationContext(),AppSettings.graphicsDir);   

			if (useGL == 0)
				args = "--ren soft";

			String[] args_array = Utils.creatArgs(args);

			int ret = NativeLib.init(AppSettings.graphicsDir,64,args_array,useGL,gamePath);

			Log.i("Quake2", "Quake2Init done");

		}



		//// new Renderer interface
		int notifiedflags;

		FPSLimit fpsLimit;

		boolean inited = false;

		public void onDrawFrame(GL10 gl) {

			if (!inited)
			{
				SDLActivity.nativeInit();

				AppSettings.setIntOption(getApplicationContext(), "max_fps", 0);  

				inited = true;
				init( surfaceWidth, surfaceHeight );
			}
		}




		public void onSurfaceChanged(GL10 gl, int width, int height) {


			Log.d("Renderer", String.format("onSurfaceChanged %dx%d", width,height) );

			//			SDLLib.nativeInit(false);
			//		SDLLib.surfaceChanged(PixelFormat.RGBA_8888, width, height);

			SDLActivity.onNativeResize(width, height,PixelFormat.RGBA_8888);

			//SDLLib.surfaceChanged(PixelFormat.RGBA_8888, 320, 240);


			controlInterp.setScreenSize(width, height);

			surfaceWidth = width;
			surfaceHeight = height;

		}


	} // end of QuakeRenderer


}


