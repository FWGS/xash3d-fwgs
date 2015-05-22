package com.beloko.touchcontrols;

import in.celest.xash3d.hl.R;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.io.Serializable;
import java.util.ArrayList;

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.DialogInterface.OnDismissListener;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.CheckBox;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

import com.beloko.touchcontrols.Settings.IDGame;

public class ControlConfig implements Serializable{


	final String LOG = "QuakeControlConfig";

	/**
	 * 
	 */
	private static final long serialVersionUID = 1L;

	enum Type {ANALOG,BUTTON,MENU};

	public static final int LOOK_MODE_MOUSE    =0;
	public static final int LOOK_MODE_ABSOLUTE =1;
	public static final int LOOK_MODE_JOYSTICK =2;

	public static final int ACTION_ANALOG_FWD       = 0x100;
	public static final int ACTION_ANALOG_STRAFE    = 0x101;
	public static final int ACTION_ANALOG_PITCH     = 0x102;
	public static final int ACTION_ANALOG_YAW       = 0x103;

	public static final int PORT_ACT_LEFT       =1;
	public static final int PORT_ACT_RIGHT      =2;
	public static final int PORT_ACT_FWD        =3;
	public static final int PORT_ACT_BACK       =4;
	public static final int PORT_ACT_LOOK_UP    =5;
	public static final int PORT_ACT_LOOK_DOWN  =6;
	public static final int PORT_ACT_MOVE_LEFT  =7;
	public static final int PORT_ACT_MOVE_RIGHT =8;
	public static final int PORT_ACT_STRAFE     =9;
	public static final int PORT_ACT_SPEED      =10;
	public static final int PORT_ACT_USE        =11;
	public static final int PORT_ACT_JUMP       =12;
	public static final int PORT_ACT_ATTACK     =13;
	public static final int PORT_ACT_UP         =14;
	public static final int PORT_ACT_DOWN       =15;

	public static final int PORT_ACT_NEXT_WEP   =16;
	public static final int PORT_ACT_PREV_WEP   =17;

	//Quake 2
	public static final int PORT_ACT_INVEN     = 18;
	public static final int PORT_ACT_INVUSE    = 19;
	public static final int PORT_ACT_INVDROP   = 20;
	public static final int PORT_ACT_INVPREV   = 21;
	public static final int PORT_ACT_INVNEXT   = 22;
	public static final int PORT_ACT_HELPCOMP  = 23;

	//Doom
	public static final int  PORT_ACT_MAP          = 30;
	public static final int  PORT_ACT_MAP_UP       = 31;
	public static final int  PORT_ACT_MAP_DOWN     = 32;
	public static final int  PORT_ACT_MAP_LEFT     = 33;
	public static final int  PORT_ACT_MAP_RIGHT    = 34;
	public static final int  PORT_ACT_MAP_ZOOM_IN  = 35;
	public static final int  PORT_ACT_MAP_ZOOM_OUT = 36;

	//RTCW
	public static final int  PORT_ACT_ZOOM_IN   = 50;
	public static final int  PORT_ACT_ALT_FIRE  = 51;
	public static final int  PORT_ACT_RELOAD    = 52;
	public static final int  PORT_ACT_QUICKSAVE = 53;
	public static final int  PORT_ACT_QUICKLOAD = 54;
	public static final int  PORT_ACT_KICK      = 56;
	public static final int  PORT_ACT_LEAN_LEFT =  57;
	public static final int  PORT_ACT_LEAN_RIGHT = 58;

	//JK2
	//public static final int   PORT_ACT_FORCE_LIGHTNING = 60;
	//public static final int   PORT_ACT_SABER_BLOCK     = 62;
	//public static final int   PORT_ACT_FORCE_GRIP      = 63;
	public static final int   PORT_ACT_ALT_ATTACK      = 64;
	public static final int   PORT_ACT_NEXT_FORCE      = 65;
	public static final int   PORT_ACT_PREV_FORCE      = 66;
	public static final int   PORT_ACT_FORCE_USE       = 67;
	public static final int   PORT_ACT_DATAPAD         = 68;
	public static final int   PORT_ACT_FORCE_SELECT    = 69;
	public static final int   PORT_ACT_WEAPON_SELECT   = 70;
	public static final int   PORT_ACT_SABER_STYLE     = 71;
	public static final int   PORT_ACT_FORCE_PULL      = 75;
	public static final int   PORT_ACT_FORCE_MIND      = 76;
	public static final int   PORT_ACT_FORCE_LIGHT     = 77;
	public static final int   PORT_ACT_FORCE_HEAL      = 78;
	public static final int   PORT_ACT_FORCE_GRIP      = 79;
	public static final int   PORT_ACT_FORCE_SPEED     = 80;
	public static final int   PORT_ACT_FORCE_PUSH      = 81;	
	public static final int   PORT_ACT_SABER_SEL       = 87; //Just chooses weapon 1 so show/hide saber.

	//Choloate
	public static final int   PORT_ACT_GAMMA           =   90;
	public static final int   PORT_ACT_SHOW_WEAPONS    =   91;
	public static final int   PORT_ACT_SHOW_KEYS       =   92;
	public static final int   PORT_ACT_FLY_UP          =   93;
	public static final int   PORT_ACT_FLY_DOWN        =   94;

	//Custom
	public static final int PORT_ACT_CUSTOM_0          = 150;
	public static final int PORT_ACT_CUSTOM_1          = 151;
	public static final int PORT_ACT_CUSTOM_2          = 152;
	public static final int PORT_ACT_CUSTOM_3          = 153;
	public static final int PORT_ACT_CUSTOM_4          = 154;
	public static final int PORT_ACT_CUSTOM_5          = 155;
	public static final int PORT_ACT_CUSTOM_6          = 156;
	public static final int PORT_ACT_CUSTOM_7          = 157;


	
	//Menu
	public static final int MENU_UP                 = 0x200;
	public static final int MENU_DOWN               = 0x201;
	public static final int MENU_LEFT               = 0x202;
	public static final int MENU_RIGHT              = 0x203;
	public static final int MENU_SELECT             = 0x204;
	public static final int MENU_BACK               = 0x205;


	Context ctx;
	TextView infoTextView;

	String filename;

	boolean ignoreDirectionFromJoystick;

	public ControlConfig(String file,IDGame game)
	{
		actions.add(new ActionInput("analog_move_fwd","Forward/Back",ACTION_ANALOG_FWD,Type.ANALOG));
		actions.add(new ActionInput("analog_move_strafe","Strafe",ACTION_ANALOG_STRAFE,Type.ANALOG));


		if (game != IDGame.Wolf3d)
			actions.add(new ActionInput("analog_look_pitch","Look Up/Look Down",ACTION_ANALOG_PITCH,Type.ANALOG));

		actions.add(new ActionInput("analog_look_yaw","Look Left/Look Right",ACTION_ANALOG_YAW,Type.ANALOG));

		actions.add(new ActionInput("attack","Attack",PORT_ACT_ATTACK,Type.BUTTON));

		if ((game == IDGame.Doom) || (game == IDGame.Wolf3d)|| (game == IDGame.Hexen)|| (game == IDGame.Strife)|| (game == IDGame.Heretic))
			actions.add(new ActionInput("use","Use/Open",PORT_ACT_USE,Type.BUTTON));

		if (game == IDGame.RTCW)
		{
			actions.add(new ActionInput("use","Use/Open",PORT_ACT_USE,Type.BUTTON));
			actions.add(new ActionInput("reload","Reload",PORT_ACT_RELOAD,Type.BUTTON));
			actions.add(new ActionInput("alt_fire","Alt Weapon",PORT_ACT_ALT_FIRE,Type.BUTTON));
			actions.add(new ActionInput("binocular","Binocuar",PORT_ACT_ZOOM_IN,Type.BUTTON));
			actions.add(new ActionInput("quick_kick","Kick",PORT_ACT_KICK,Type.BUTTON));
			actions.add(new ActionInput("lean_left","Lean Left",PORT_ACT_LEAN_LEFT,Type.BUTTON));
			actions.add(new ActionInput("lean_right","Lean Right",PORT_ACT_LEAN_RIGHT,Type.BUTTON));
		}

		if (game == IDGame.Quake3)
		{
			actions.add(new ActionInput("zoomin","Zoom in/out",PORT_ACT_ZOOM_IN,Type.BUTTON));
			actions.add(new ActionInput("custom_0","Custom F1",PORT_ACT_CUSTOM_0,Type.BUTTON));
			actions.add(new ActionInput("custom_1","Custom F2",PORT_ACT_CUSTOM_1,Type.BUTTON));
			actions.add(new ActionInput("custom_2","Custom F3",PORT_ACT_CUSTOM_2,Type.BUTTON));
			actions.add(new ActionInput("custom_3","Custom F4",PORT_ACT_CUSTOM_3,Type.BUTTON));
		} 
		
		if ((game == IDGame.JK2) || (game == IDGame.JK3))
		{
			actions.add(new ActionInput("attack_alt","Alt Attack",PORT_ACT_ALT_ATTACK,Type.BUTTON));
			actions.add(new ActionInput("use_force","Use Force",PORT_ACT_FORCE_USE,Type.BUTTON));
			actions.add(new ActionInput("saber_style","Saber Style",PORT_ACT_SABER_STYLE,Type.BUTTON));
			actions.add(new ActionInput("saber_show_hide","Saber Sheath/Unsheath",PORT_ACT_SABER_SEL,Type.BUTTON));
			actions.add(new ActionInput("use","Use/Open",PORT_ACT_USE,Type.BUTTON));
		}

		if ((game != IDGame.Doom) && (game != IDGame.Wolf3d))
			actions.add(new ActionInput("jump","Jump",PORT_ACT_JUMP,Type.BUTTON));

		if ((game == IDGame.Quake2) || (game == IDGame.Quake3)|| (game == IDGame.Hexen2)|| (game == IDGame.RTCW)|| (game == IDGame.JK2) || (game == IDGame.JK3))
			actions.add(new ActionInput("crouch","Crouch",PORT_ACT_DOWN,Type.BUTTON));

		//Add GZDoom specific actions
		if (game == IDGame.Doom)
		{
			actions.add(new ActionInput("attack_alt","Alt Attack (GZ)",PORT_ACT_ALT_ATTACK,Type.BUTTON));
			actions.add(new ActionInput("jump","Jump (GZ)",PORT_ACT_JUMP,Type.BUTTON));
			actions.add(new ActionInput("crouch","Crouch (GZ)",PORT_ACT_DOWN,Type.BUTTON));
			actions.add(new ActionInput("custom_0","Custom A (GZ)",PORT_ACT_CUSTOM_0,Type.BUTTON));
			actions.add(new ActionInput("custom_1","Custom B (GZ)",PORT_ACT_CUSTOM_1,Type.BUTTON));
			actions.add(new ActionInput("custom_2","Custom C (GZ)",PORT_ACT_CUSTOM_2,Type.BUTTON));
			actions.add(new ActionInput("custom_3","Custom D (GZ)",PORT_ACT_CUSTOM_3,Type.BUTTON));
			actions.add(new ActionInput("custom_4","Custom E (GZ)",PORT_ACT_CUSTOM_4,Type.BUTTON));
			actions.add(new ActionInput("custom_5","Custom F (GZ)",PORT_ACT_CUSTOM_5,Type.BUTTON));
			actions.add(new ActionInput("quick_save","Quick Save (GZ)",PORT_ACT_QUICKSAVE,Type.BUTTON));
			actions.add(new ActionInput("quick_load","Quick Load (GZ)",PORT_ACT_QUICKLOAD,Type.BUTTON));
			

		}
		
		actions.add(new ActionInput("fwd","Move Forward",PORT_ACT_FWD,Type.BUTTON));
		actions.add(new ActionInput("back","Move Backwards",PORT_ACT_BACK,Type.BUTTON));
		actions.add(new ActionInput("left","Strafe Left",PORT_ACT_MOVE_LEFT,Type.BUTTON));
		actions.add(new ActionInput("right","Strafe Right",PORT_ACT_MOVE_RIGHT,Type.BUTTON));

		if ((game != IDGame.Doom) && (game != IDGame.Wolf3d))
		{
			actions.add(new ActionInput("look_up","Look Up",PORT_ACT_LOOK_UP,Type.BUTTON));
			actions.add(new ActionInput("look_down","Look Down",PORT_ACT_LOOK_DOWN,Type.BUTTON));
		}

		actions.add(new ActionInput("look_left","Look Left",PORT_ACT_LEFT,Type.BUTTON));
		actions.add(new ActionInput("look_right","Look Right",PORT_ACT_RIGHT,Type.BUTTON));

		if ((game != IDGame.Wolf3d) && (game != IDGame.JK2) || (game != IDGame.JK3))
		{
			actions.add(new ActionInput("strafe_on","Strafe On",PORT_ACT_STRAFE,Type.BUTTON));
			actions.add(new ActionInput("speed","Run On",PORT_ACT_SPEED,Type.BUTTON));
		}
		actions.add(new ActionInput("next_weapon","Next Weapon",PORT_ACT_NEXT_WEP,Type.BUTTON));
		actions.add(new ActionInput("prev_weapon","Previous Weapon",PORT_ACT_PREV_WEP,Type.BUTTON));

		if ((game == IDGame.JK2)|| (game == IDGame.JK3))
		{
			actions.add(new ActionInput("next_force","Next Force",PORT_ACT_NEXT_FORCE,Type.BUTTON));
			actions.add(new ActionInput("prev_force","Previous Force",PORT_ACT_PREV_FORCE,Type.BUTTON));
			actions.add(new ActionInput("force_pull","Force Pull",PORT_ACT_FORCE_PULL,Type.BUTTON));
			actions.add(new ActionInput("force_push","Force Push",PORT_ACT_FORCE_PUSH,Type.BUTTON));
			actions.add(new ActionInput("force_speed","Force Speed",PORT_ACT_FORCE_SPEED,Type.BUTTON));
			actions.add(new ActionInput("force_heal","Force Heal",PORT_ACT_FORCE_HEAL,Type.BUTTON));
			actions.add(new ActionInput("force_mind","Force Mind",PORT_ACT_FORCE_MIND,Type.BUTTON));
			actions.add(new ActionInput("force_grip","Force Grip",PORT_ACT_FORCE_GRIP,Type.BUTTON));
			actions.add(new ActionInput("force_lightning","Force Lightning",PORT_ACT_FORCE_LIGHT,Type.BUTTON));

		}

		if ((game == IDGame.Quake2) || (game == IDGame.Hexen2)|| (game == IDGame.RTCW))
		{
			actions.add(new ActionInput("help_comp","Show Objectives",PORT_ACT_HELPCOMP,Type.BUTTON));
			actions.add(new ActionInput("inv_show","Show Inventory",PORT_ACT_INVEN,Type.BUTTON));
			actions.add(new ActionInput("inv_use","Use Item",PORT_ACT_INVUSE,Type.BUTTON));
			actions.add(new ActionInput("inv_next","Next Item",PORT_ACT_INVNEXT,Type.BUTTON));
			actions.add(new ActionInput("inv_prev","Prev Item",PORT_ACT_INVPREV,Type.BUTTON));
		}

		if (game == IDGame.JK2)
		{
			actions.add(new ActionInput("help_comp","Show Data Pad",PORT_ACT_DATAPAD,Type.BUTTON));
			actions.add(new ActionInput("inv_use","Use Item",PORT_ACT_INVUSE,Type.BUTTON));
			actions.add(new ActionInput("inv_next","Next Item",PORT_ACT_INVNEXT,Type.BUTTON));
			actions.add(new ActionInput("inv_prev","Prev Item",PORT_ACT_INVPREV,Type.BUTTON));
		}

		if (game == IDGame.Hexen)
		{
			actions.add(new ActionInput("inv_use","Use Item",PORT_ACT_INVUSE,Type.BUTTON));
			actions.add(new ActionInput("inv_next","Next Item",PORT_ACT_INVNEXT,Type.BUTTON));
			actions.add(new ActionInput("inv_prev","Prev Item",PORT_ACT_INVPREV,Type.BUTTON));
			actions.add(new ActionInput("fly_up","Fly Up",PORT_ACT_FLY_UP,Type.BUTTON));
			actions.add(new ActionInput("fly_down","Fly Down",PORT_ACT_FLY_DOWN,Type.BUTTON));

		}

		if (game == IDGame.Strife)
		{
			actions.add(new ActionInput("inv_use","Use Item",PORT_ACT_INVUSE,Type.BUTTON));
			actions.add(new ActionInput("inv_drop","Drop Item",PORT_ACT_INVDROP,Type.BUTTON));
			actions.add(new ActionInput("inv_next","Next Item",PORT_ACT_INVNEXT,Type.BUTTON));
			actions.add(new ActionInput("inv_prev","Prev Item",PORT_ACT_INVPREV,Type.BUTTON));
			actions.add(new ActionInput("show_weap","Show Stats/Weapons",PORT_ACT_SHOW_WEAPONS,Type.BUTTON));
			actions.add(new ActionInput("show_keys","Show Keys",PORT_ACT_SHOW_KEYS,Type.BUTTON));

		}

		if (game == IDGame.Heretic)
		{
			actions.add(new ActionInput("inv_use","Use Item",PORT_ACT_INVUSE,Type.BUTTON));
			actions.add(new ActionInput("inv_next","Next Item",PORT_ACT_INVNEXT,Type.BUTTON));
			actions.add(new ActionInput("inv_prev","Prev Item",PORT_ACT_INVPREV,Type.BUTTON));
			actions.add(new ActionInput("fly_up","Fly Up",PORT_ACT_FLY_UP,Type.BUTTON));
			actions.add(new ActionInput("fly_down","Fly Down",PORT_ACT_FLY_DOWN,Type.BUTTON));
		}

		if (game == IDGame.Quake3)
		{
			actions.add(new ActionInput("inv_use","Use Item",PORT_ACT_USE,Type.BUTTON));
		}

		if (game == IDGame.Doom)
		{
			actions.add(new ActionInput("map_show","Show Automap",PORT_ACT_MAP,Type.BUTTON));
			actions.add(new ActionInput("map_up","Automap Up",PORT_ACT_MAP_UP,Type.BUTTON));
			actions.add(new ActionInput("map_down","Automap Down",PORT_ACT_MAP_DOWN,Type.BUTTON));
			actions.add(new ActionInput("map_left","Automap Left",PORT_ACT_MAP_LEFT,Type.BUTTON));
			actions.add(new ActionInput("map_right","Automap Right",PORT_ACT_MAP_RIGHT,Type.BUTTON));
			actions.add(new ActionInput("map_zoomin","Automap Zoomin",PORT_ACT_MAP_ZOOM_IN,Type.BUTTON));
			actions.add(new ActionInput("map_zoomout","Automap Zoomout",PORT_ACT_MAP_ZOOM_OUT,Type.BUTTON));
		}

		if ((game == IDGame.RTCW) || (game == IDGame.JK2) || (game == IDGame.JK3))
		{
			actions.add(new ActionInput("quick_save","Quick Save",PORT_ACT_QUICKSAVE,Type.BUTTON));
			actions.add(new ActionInput("quick_load","Quick Load",PORT_ACT_QUICKLOAD,Type.BUTTON));	
		}

		if ((game == IDGame.Doom) || (game == IDGame.Heretic)  || (game == IDGame.Hexen) 
				 || (game == IDGame.Strife)|| (game == IDGame.Quake)|| (game == IDGame.Quake2)
				 || (game == IDGame.Hexen2)
				 || (game == IDGame.JK2)  || (game == IDGame.JK3))
		{
			actions.add(new ActionInput("menu_up","Menu Up",MENU_UP,Type.MENU));
			actions.add(new ActionInput("menu_down","Menu Down",MENU_DOWN,Type.MENU));
			actions.add(new ActionInput("menu_left","Menu Left",MENU_LEFT,Type.MENU));
			actions.add(new ActionInput("menu_right","Menu Right",MENU_RIGHT,Type.MENU));
			actions.add(new ActionInput("menu_select","Menu Select",MENU_SELECT,Type.MENU));
			actions.add(new ActionInput("menu_back","Menu Back",MENU_BACK,Type.MENU));
		}
		filename = file;
	}

	public void setTextView(Context c,TextView tv)
	{
		ctx = c;
		infoTextView = tv;
	}

	void saveControls(File file) throws IOException
	{
		if (Settings.DEBUG) Log.d(LOG,"saveControls, file = " + file.toString());

		FileOutputStream fos = null;
		ObjectOutputStream out = null;

		fos = new FileOutputStream(file);
		out = new ObjectOutputStream(fos);
		out.writeObject(actions);
		out.close();
	}

	public void loadControls() throws IOException, ClassNotFoundException
	{
		loadControls(new File(filename));
	}

	public void loadControls(File file) throws IOException, ClassNotFoundException
	{
		if (Settings.DEBUG) Log.d(LOG,"loadControls, file = " + file.toString());

		InputStream fis = null;
		ObjectInputStream in = null; 


		fis = new FileInputStream(file);

		in = new ObjectInputStream(fis);
		ArrayList<ActionInput> cd = (ArrayList<ActionInput> )in.readObject();
		if (Settings.DEBUG) Log.d(LOG,"loadControls, file loaded OK");
		in.close();

		for (ActionInput  d: cd)
		{
			for (ActionInput  a: actions)
			{
				if (d.tag.contentEquals(a.tag))
				{
					a.invert = d.invert;
					a.source = d.source;
					a.sourceType = d.sourceType;
					a.sourcePositive = d.sourcePositive;
					a.scale = d.scale;
					if (a.scale == 0) a.scale = 1;
				}
			}
		}

		//Now check no buttons are also assigned to analog, if it is, clear the buttons
		//This is because n00bs keep assigning movment analog AND buttons!
		for (ActionInput  a: actions)
		{
			if ((a.source != -1) && (a.sourceType == Type.ANALOG) && (a.actionType == Type.BUTTON))
			{
				for (ActionInput  a_check: actions)
				{
					if ((a_check.sourceType == Type.ANALOG) && (a_check.actionType == Type.ANALOG))
					{
						if (a.source == a_check.source)
						{
							a.source = -1;
							break;
						}
					}
				}
			}
		}

		fis.close();
	}


	void updated()
	{
		try {
			saveControls(new File (filename));
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
	}

	ArrayList<ActionInput> actions = new ArrayList<ActionInput>(); 

	ActionInput actionMontor=null;

	boolean monitoring = false;

	public  boolean showExtraOptions(Activity act,int pos)
	{
		final ActionInput in = actions.get(pos);

		if (in.actionType == Type.ANALOG)
		{
			Dialog dialog = new Dialog(act);
			dialog.setTitle("Axis Sensitivity Setting");
			dialog.setCancelable(true);

			final LinearLayout l = new LinearLayout(act);
			l.setOrientation(LinearLayout.VERTICAL);

			final SeekBar sb = new SeekBar(act);
			l.addView(sb);


			sb.setMax(100);
			sb.setProgress((int)(in.scale * 50));



			final CheckBox invert = new CheckBox(act);
			invert.setText("Invert");
			invert.setChecked(in.invert);

			l.addView(invert);

			dialog.setOnDismissListener(new OnDismissListener() {

				@Override
				public void onDismiss(DialogInterface dialog) {
					in.scale = (float)sb.getProgress()/(float)50;
					in.invert = invert.isChecked();
					updated();
				}
			});

			dialog.setContentView(l);

			dialog.show();
			return true;
		}
		return false;
	}

	public void startMonitor(Activity act,int pos)
	{
		actionMontor = actions.get(pos);
		monitoring = true;

		if (actionMontor.actionType == Type.ANALOG)
			infoTextView.setText("Move Stick for: " + actionMontor.description);
		else
			infoTextView.setText("Press Button for: " + actionMontor.description);

		infoTextView.setTextColor(ctx.getResources().getColor(android.R.color.holo_green_light));
	}

	int[] axisTest = {
			/*
			MotionEvent.AXIS_GENERIC_1,
			MotionEvent.AXIS_GENERIC_2,
			MotionEvent.AXIS_GENERIC_3,
			MotionEvent.AXIS_GENERIC_4,
			MotionEvent.AXIS_GENERIC_5,
			MotionEvent.AXIS_GENERIC_6,
			MotionEvent.AXIS_GENERIC_7,
			MotionEvent.AXIS_GENERIC_8,
			MotionEvent.AXIS_GENERIC_9,
			MotionEvent.AXIS_GENERIC_10,
			MotionEvent.AXIS_GENERIC_11,
			MotionEvent.AXIS_GENERIC_12,
			MotionEvent.AXIS_GENERIC_13,
			MotionEvent.AXIS_GENERIC_14,
			MotionEvent.AXIS_GENERIC_15,
			MotionEvent.AXIS_GENERIC_16,
			 */
			MotionEvent.AXIS_HAT_X,
			MotionEvent.AXIS_HAT_Y,
			MotionEvent.AXIS_LTRIGGER,
			MotionEvent.AXIS_RTRIGGER,
			MotionEvent.AXIS_RUDDER,
			MotionEvent.AXIS_RX,
			MotionEvent.AXIS_RY,
			MotionEvent.AXIS_RZ,
			MotionEvent.AXIS_THROTTLE,
			MotionEvent.AXIS_X,
			MotionEvent.AXIS_Y,
			MotionEvent.AXIS_Z,
			MotionEvent.AXIS_BRAKE,
			MotionEvent.AXIS_GAS,
	};

	public boolean onGenericMotionEvent(GenericAxisValues event)
	{
		Log.d(LOG,"onGenericMotionEvent");
		if (monitoring)
		{
			if (actionMontor != null)
			{
				for (int a: axisTest)
				{
					if (Math.abs(event.getAxisValue(a)) > 0.6)
					{
						actionMontor.source = a;
						actionMontor.sourceType = Type.ANALOG;
						//Used for button actions
						if (event.getAxisValue(a) > 0)
							actionMontor.sourcePositive = true;
						else
							actionMontor.sourcePositive = false;

						monitoring = false;

						if (Settings.DEBUG) Log.d(LOG,actionMontor.description + " = Analog (" + actionMontor.source + ")");

						infoTextView.setText("Select Action");
						infoTextView.setTextColor(ctx.getResources().getColor(android.R.color.holo_blue_light));

						updated();
						return true;
					}	
				}
			}
		}
		return false;
	}

	public boolean isMonitoring()
	{
		return monitoring;
	}

	public boolean onKeyDown(int keyCode, KeyEvent event)
	{
		Log.d(LOG,"onKeyDown " + keyCode);

		if (monitoring)
		{
			if (keyCode == KeyEvent.KEYCODE_BACK) //Cancel and clear button assignment
			{
				actionMontor.source = -1;
				actionMontor.sourceType = Type.BUTTON;
				monitoring = false;
				infoTextView.setText("CANCELED");
				infoTextView.setTextColor(ctx.getResources().getColor(android.R.color.holo_red_light));

				updated();
				return true;
			}
			else
			{

				if (actionMontor != null)
				{
					if (actionMontor.actionType != Type.ANALOG)
					{
						actionMontor.source = keyCode;
						actionMontor.sourceType = Type.BUTTON;
						monitoring = false;

						infoTextView.setText("Select Action");
						infoTextView.setTextColor(ctx.getResources().getColor(android.R.color.holo_blue_light));

						updated();
						return true;
					}
				}
			}
		}


		return false;
	}

	public boolean onKeyUp(int keyCode, KeyEvent event)
	{
		return false;
	} 


	public int getSize()
	{
		return actions.size();
	}

	public View getView(final Activity ctx,final int nbr)
	{

		View view = ctx.getLayoutInflater().inflate(R.layout.controls_listview_item, null);
		ImageView image = (ImageView)view.findViewById(R.id.imageView);
		TextView name = (TextView)view.findViewById(R.id.name_textview);
		TextView binding = (TextView)view.findViewById(R.id.binding_textview);
		ImageView setting_image = (ImageView)view.findViewById(R.id.settings_imageview);

		ActionInput ai = actions.get(nbr);

		if ((ai.actionType == Type.BUTTON) || (ai.actionType == Type.MENU))
		{
			
			if (ai.sourceType == Type.ANALOG)
				binding.setText(MotionEvent.axisToString(ai.source));
			else
				binding.setText(KeyEvent.keyCodeToString(ai.source));

			setting_image.setVisibility(View.GONE);

			if ( (ai.actionType == Type.MENU))
			{
				name.setTextColor(0xFF00aeef); //BLUEY
				image.setImageResource(R.drawable.gamepad_menu);
			}
			else
			{
				image.setImageResource(R.drawable.gamepad);
			}
		}
		else if (ai.actionType == Type.ANALOG)
		{
			binding.setText(MotionEvent.axisToString(ai.source));
			setting_image.setOnClickListener(new OnClickListener() {

				@Override
				public void onClick(View v) {
					showExtraOptions(ctx,nbr);
				}
			});
			name.setTextColor(0xFFf7941d); //ORANGE
		}
		
		/*
		if (ai.actionType == Type.BUTTON)
		{
			image.setImageResource(R.drawable.gamepad);
			if (ai.sourceType == Type.ANALOG)
				binding.setText(MotionEvent.axisToString(ai.source));
			else
				binding.setText(KeyEvent.keyCodeToString(ai.source));

			setting_image.setVisibility(View.GONE);
		}
		else //Analog
		{
			binding.setText(MotionEvent.axisToString(ai.source));
			setting_image.setOnClickListener(new OnClickListener() {

				@Override
				public void onClick(View v) {
					showExtraOptions(ctx,nbr);
				}
			});
			name.setTextColor(0xFFf7941d);
		}
*/
		name.setText(ai.description);

		return  view;
	}
}
