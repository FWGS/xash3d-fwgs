package com.beloko.touchcontrols;

import in.celest.xash3d.hl.R;

import java.io.IOException;

import android.app.Activity;
import android.app.Fragment;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.AdapterView.OnItemLongClickListener;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.CompoundButton.OnCheckedChangeListener;
import android.widget.ListView;
import android.widget.TextView;

import com.bda.controller.Controller;
import com.bda.controller.ControllerListener;
import com.bda.controller.StateEvent;

public class GamePadFragment extends Fragment{
	final String LOG = "GamePadFragment";
	
	ListView listView;
	ControlListAdapter adapter;

	TextView info;

	ControlConfig config;

	GenericAxisValues genericAxisValues = new GenericAxisValues();
	
	Controller mogaController = null;
	final MogaControllerListener mListener = new MogaControllerListener();
	
	@Override
	public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
			
		config = new ControlConfig(Settings.gamePadControlsFile,Settings.game);
		
		try {
			config.loadControls();
		} catch (IOException e) {
			// TODO Auto-generated catch block
			//e.printStackTrace();
		} catch (ClassNotFoundException e) {
			// TODO Auto-generated catch block
			//e.printStackTrace();
		}
		
		
		mogaController = Controller.getInstance(getActivity());
		mogaController.init();
		mogaController.setListener(mListener,new Handler());
	}

	
	boolean isHidden = true;
	@Override
	public void onHiddenChanged(boolean hidden) {
		isHidden = hidden;
		super.onHiddenChanged(hidden);
	}



	@Override
	public void onPause()
	{
		super.onPause();
		mogaController.onPause();
	}
	@Override
	public void onResume()
	{
		super.onResume();
		mogaController.onResume();
	}
	
	@Override
	public void onDestroy()
	{
		super.onDestroy();
		mogaController.exit();
	}
	
	
	@Override
	public View onCreateView(LayoutInflater inflater, ViewGroup container,
			Bundle savedInstanceState) {
		View mainView = inflater.inflate(R.layout.fragment_gamepad, null);


		CheckBox enableCb = (CheckBox)mainView.findViewById(R.id.gamepad_enable_checkbox);
		enableCb.setChecked(Settings.gamePadEnabled);

		enableCb.setOnCheckedChangeListener(new OnCheckedChangeListener() {

			@Override
			public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
				Settings.setBoolOption(getActivity(), "gamepad_enabled", isChecked);
				Settings.gamePadEnabled = isChecked;
				setListViewEnabled(Settings.gamePadEnabled);

			}
		});


		CheckBox hideCtrlCb = (CheckBox)mainView.findViewById(R.id.gamepad_hide_touch_checkbox);
		hideCtrlCb.setChecked(Settings.hideTouchControls);

		hideCtrlCb.setOnCheckedChangeListener(new OnCheckedChangeListener() {

			@Override
			public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
				Settings.setBoolOption(getActivity(), "hide_touch_controls", isChecked);
				Settings.hideTouchControls = isChecked;
			}
		});
		
		
		Button help = (Button)mainView.findViewById(R.id.gamepad_help_button);
		help.setOnClickListener(new OnClickListener() {
			
			@Override
			public void onClick(View v) {
				//NoticeDialog.show(getActivity(),"Gamepad Help", R.raw.gamepad);
			}
		});
		
		listView = (ListView)mainView.findViewById(R.id.gamepad_listview);
		adapter = new ControlListAdapter(getActivity());
		listView.setAdapter(adapter);

		setListViewEnabled(Settings.gamePadEnabled);


		listView.setSelector(R.drawable.layout_sel_background);
		listView.setOnItemClickListener(new OnItemClickListener() {

			@Override
			public void onItemClick(AdapterView<?> arg0, View v, int pos,
					long id) {
				config.startMonitor(getActivity(), pos);
			}
		});

		listView.setOnItemLongClickListener(new OnItemLongClickListener() {

			@Override
			public boolean onItemLongClick(AdapterView<?> arg0, View v, int pos,
					long id) {
				return config.showExtraOptions(getActivity(), pos);
			}
		});

		adapter.notifyDataSetChanged();

		info = (TextView)mainView.findViewById(R.id.gamepad_info_textview);
		info.setText("Select Action");
		info.setTextColor(getResources().getColor(android.R.color.holo_blue_light));

		config.setTextView(getActivity(),info);

		return mainView;
	}

	private void setListViewEnabled(boolean v)
	{

		listView.setEnabled(v);
		if (v)
		{
			listView.setAlpha(1);
		}
		else
		{
			listView.setAlpha(0.3f); 
			//listView.setBackgroundColor(Color.GRAY);
		}
	}

	public boolean onGenericMotionEvent(MotionEvent event)
	{
		genericAxisValues.setAndroidValues(event);
		
		if (config.onGenericMotionEvent(genericAxisValues))
			adapter.notifyDataSetChanged();
		
		//return config.isMonitoring(); //This does not work, mouse appears anyway
		return !isHidden; //If gamepas tab visible always steal
	}

	public boolean onKeyDown(int keyCode, KeyEvent event)
	{
		if (config.onKeyDown(keyCode, event))
		{
			adapter.notifyDataSetChanged();
			return true;
		}
		return false;
	}

	public boolean onKeyUp(int keyCode, KeyEvent event)
	{
		if(config.onKeyUp(keyCode, event))
		{
			adapter.notifyDataSetChanged();
			return true;
		}
		return false;
	} 

	class ControlListAdapter extends BaseAdapter{
		private Activity context;

		public ControlListAdapter(Activity context){
			this.context=context;

		}
		public void add(String string){

		}
		public int getCount() {
			return config.getSize();
		}

		public Object getItem(int arg0) {
			// TODO Auto-generated method stub
			return null;
		}

		public long getItemId(int arg0) {
			// TODO Auto-generated method stub
			return 0;
		}


		public View getView (int position, View convertView, ViewGroup list)  {
			View v = config.getView(getActivity(), position);
			return v;
		}

	}


	class MogaControllerListener implements ControllerListener {

		
		@Override
		public void onKeyEvent(com.bda.controller.KeyEvent event) {
			//Log.d(LOG,"onKeyEvent " + event.getKeyCode());
			
			if (event.getAction() == com.bda.controller.KeyEvent.ACTION_DOWN)
				onKeyDown(event.getKeyCode(),null);
			else if (event.getAction() == com.bda.controller.KeyEvent.ACTION_UP)
				onKeyUp(event.getKeyCode(),null);
		}
	
		@Override
		public void onMotionEvent(com.bda.controller.MotionEvent event) {
			//Log.d(LOG,"onGenericMotionEvent " + event.toString());
			
			genericAxisValues.setMogaValues(event);
			
			if (config.onGenericMotionEvent(genericAxisValues))
				adapter.notifyDataSetChanged();
		}

		@Override
		public void onStateEvent(StateEvent event) {
			Log.d(LOG,"onStateEvent " + event.getState());
		}
	}

}
