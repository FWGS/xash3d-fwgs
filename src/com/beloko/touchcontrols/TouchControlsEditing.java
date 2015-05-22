package com.beloko.touchcontrols; 

import in.celest.xash3d.hl.R;
import android.app.Activity;
import android.app.Dialog;
import android.graphics.drawable.BitmapDrawable;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.BaseAdapter;
import android.widget.CompoundButton;
import android.widget.CompoundButton.OnCheckedChangeListener;
import android.widget.ImageView;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.ToggleButton;

public class TouchControlsEditing { 

	static String TAG = "TouchControlsEditing";

	static class ControlInfo
	{
		String tag;
		String image;
		boolean enabled;
		boolean hidden;
	}  

	static ListAdapter adapter;

	static Activity activity;

	public  static native void  JNIGetControlInfo(int pos,ControlInfo info);

	public  static native int JNIGetNbrControls();

	public  static native void  JNISetHidden(int pos, boolean hidden);

	public static void setup(Activity a)
	{
		activity = a;
	}

	public static void show()
	{
		show(activity);
	}
	
	public static void show(Activity act)
	{
		Log.d(TAG,"showSettings");

		if (act != null)
			activity = act;
		
		activity.runOnUiThread(new Runnable(){
			public void run() {
				final Dialog dialog = new Dialog(activity);
				ListView listView = new ListView(activity);

				dialog.setContentView(listView);
				dialog.setTitle("Add/remove buttons");
				dialog.setCancelable(true);

				adapter = new ListAdapter(activity);
				listView.setAdapter(adapter);

				dialog.getWindow().setFlags(
						WindowManager.LayoutParams.FLAG_FULLSCREEN, 
						WindowManager.LayoutParams.FLAG_FULLSCREEN);

				dialog.show();
			}
		});

	}

	static class ListAdapter extends BaseAdapter{
		private Activity context;

		public ListAdapter(Activity context){
			this.context=context;

		}
		public void add(String string){

		}
		public int getCount() {
			return TouchControlsEditing.JNIGetNbrControls();
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
			//if (convertView == null) dont reuse view otherwise check change get called 
			convertView = activity.getLayoutInflater().inflate(R.layout.edit_controls_listview_item, null);

			final int my_pos = position;

			ImageView image = (ImageView)convertView.findViewById(R.id.imageView);
			TextView name = (TextView)convertView.findViewById(R.id.name_textview);
			ToggleButton hidden =  (ToggleButton)convertView.findViewById(R.id.hidden_switch);


			TouchControlsEditing.ControlInfo ci = new TouchControlsEditing.ControlInfo();
			TouchControlsEditing.JNIGetControlInfo(position, ci);

			name.setText(ci.tag);
			hidden.setChecked(!ci.hidden);
			hidden.setTag(new Integer(position));

			hidden.setOnCheckedChangeListener(new OnCheckedChangeListener() {

				@Override
				public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
					Integer pos = (Integer)buttonView.getTag();

					TouchControlsEditing.JNISetHidden(pos, !isChecked);
					adapter.notifyDataSetChanged();
				}
			});

			String png = activity.getFilesDir() + "/" + ci.image + ".png";
			Log.d(TAG,"png = " + png);
			BitmapDrawable bm = new BitmapDrawable(png);

			image.setImageDrawable(bm);
			return convertView;
		}

	}


}
