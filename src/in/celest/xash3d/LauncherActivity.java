package in.celest.xash3d;

import android.app.*;
import android.content.*;
import android.graphics.*;
import android.graphics.drawable.*;
import android.net.*;
import android.os.*;
import android.text.*;
import android.text.method.*;
import android.text.style.*;
import android.util.*;
import android.view.*;
import android.widget.*;
import in.celest.xash3d.hl.*;
import java.io.*;
import java.net.*;
import org.json.*;
import android.preference.*;

public class LauncherActivity extends Activity {
   // public final static String ARGV = "in.celest.xash3d.MESSAGE";
   	public final static int sdk = Integer.valueOf(Build.VERSION.SDK);
	public final static String UPDATE_LINK = "https://api.github.com/repos/FWGS/xash3d-android-project/releases"; // releases/latest doesn't return prerelease and drafts
	static EditText cmdArgs;
	static EditText resPath;
	static ToggleButton useVolume;
	static ToggleButton resizeWorkaround;
	static CheckBox	checkUpdates;
	static CheckBox updateToBeta;
	static CheckBox immersiveMode;
	static SharedPreferences mPref;
	static Spinner pixelSpinner;
	static TextView tvResPath;
	static EditText resScale, resWidth, resHeight;
	static RadioButton radioScale, radioCustom;
	static CheckBox resolution;
	String getDefaultPath()
	{
		File dir = Environment.getExternalStorageDirectory();
		if( dir != null && dir.exists() )
			return dir.getPath() + "/xash";
		return "/sdcard/xash";
	}
	public static void changeButtonsStyle(ViewGroup parent) {
		if(sdk >= 21)
			return;
        for (int i = parent.getChildCount() - 1; i >= 0; i--) {
			try{
            final View child = parent.getChildAt(i);
			if( child == null )
				continue;
            if (child instanceof ViewGroup) {
                changeButtonsStyle((ViewGroup) child);
                // DO SOMETHING WITH VIEWGROUP, AFTER CHILDREN HAS BEEN LOOPED
            } else if (child instanceof Button)  {
				final Button b = (Button)child;
				final Drawable bg = b.getBackground();
				if(bg!= null)bg.setAlpha(96);
				b.setTextColor(0xFFFFFFFF);
				b.setTextSize(15f);
				//b.setText(b.getText().toString().toUpperCase());
				b.setTypeface(b.getTypeface(),Typeface.BOLD);
            }else if (child instanceof EditText)  {
				final EditText b = (EditText)child;
				b.setBackgroundColor(0xFF353535);
				b.setTextColor(0xFFFFFFFF);
				b.setTextSize(15f);
				}
			}
			catch(Exception e){}
        }
    }
    
    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
		super.onCreate(savedInstanceState);
		this.requestWindowFeature(Window.FEATURE_NO_TITLE);
		//super.setTheme( 0x01030005 );
		if ( sdk >= 21 )
			super.setTheme( 0x01030224 );
		else super.setTheme( 0x01030005 );
		
		if( CertCheck.dumbAntiPDALifeCheck( this ) )
		{
			finish();
			return;
		}
		
		setContentView(R.layout.activity_launcher);

		TabHost tabHost = (TabHost) findViewById(R.id.tabhost);

		tabHost.setup();
		
		TabHost.TabSpec tabSpec;
		tabSpec = tabHost.newTabSpec("tabtag1");
		tabSpec.setIndicator(getString(R.string.text_tab1));
		tabSpec.setContent(R.id.tab1);
		tabHost.addTab(tabSpec);

		tabSpec = tabHost.newTabSpec("tabtag2");
		tabSpec.setIndicator(getString(R.string.text_tab2));
		tabSpec.setContent(R.id.tab2);
		tabHost.addTab(tabSpec);
		if( sdk < 21 )
		{
			try
			{
				tabHost.invalidate();
				for(int i = 0; i < tabHost.getTabWidget().getChildCount(); i++)
				{
				tabHost.getTabWidget().getChildAt(i).getBackground().setAlpha(96);
				tabHost.getTabWidget().getChildAt(i).getLayoutParams().height = (int) (40 * this.getResources().getDisplayMetrics().density);
				}
			}
			catch(Exception e){}
		}
		

		mPref        = getSharedPreferences("engine", 0);
		cmdArgs      = (EditText) findViewById(R.id.cmdArgs);
		useVolume    = (ToggleButton) findViewById( R.id.useVolume );
		resPath      = (EditText) findViewById( R.id.cmdPath );
		checkUpdates = (CheckBox)findViewById( R.id.check_updates );
		updateToBeta = (CheckBox)findViewById( R.id.check_betas );
		pixelSpinner = (Spinner) findViewById( R.id.pixelSpinner );
		resizeWorkaround = (ToggleButton) findViewById( R.id.enableResizeWorkaround );
		tvResPath    = (TextView) findViewById( R.id.textView_path );
		immersiveMode = (CheckBox) findViewById( R.id.immersive_mode );
		resolution = (CheckBox) findViewById(R.id.resolution);
		resWidth = (EditText) findViewById(R.id.resolution_width);
		resHeight = (EditText) findViewById(R.id.resolution_height);
		resScale = (EditText) findViewById(R.id.resolution_scale);
		radioCustom = (RadioButton) findViewById(R.id.resolution_custom_r);
		radioScale = (RadioButton) findViewById(R.id.resolution_scale_r);
		
		final String[] list = {
			"32 bit (RGBA8888)",
			"24 bit (RGB888)",
			"16 bit (RGB565)",
			"16 bit (RGBA5551)",
			"16 bit (RGBA4444)",
			"8 bit (RGB332)"
		};
		ArrayAdapter<String> adapter = new ArrayAdapter<String>(this,android.R.layout.simple_spinner_item, list);
		adapter.setDropDownViewResource(android.R.layout.simple_spinner_item);
		pixelSpinner.setAdapter(adapter);
		Button selectFolderButton = ( Button ) findViewById( R.id.button_select );
	selectFolderButton.setOnClickListener(new View.OnClickListener(){
	       @Override
	    public void onClick(View v) {
	    selectFolder(v);
		}
	});
	((Button)findViewById( R.id.button_launch )).setOnClickListener(new View.OnClickListener(){
	       @Override
	    public void onClick(View v) {
	    startXash(v);
		}
	});
	((Button)findViewById( R.id.button_shortcut )).setOnClickListener(new View.OnClickListener(){
	       @Override
	    public void onClick(View v) {
	    createShortcut(v);
		}
	});
	((Button)findViewById( R.id.button_about )).setOnClickListener(new View.OnClickListener(){
	       @Override
	    public void onClick(View v) {
	    aboutXash(v);
		}
	});
		useVolume.setChecked(mPref.getBoolean("usevolume",true));
		checkUpdates.setChecked(mPref.getBoolean("check_updates",true));
		updateToBeta.setChecked(mPref.getBoolean("check_betas", false));
		updatePath(mPref.getString("basedir", getDefaultPath()));
		cmdArgs.setText(mPref.getString("argv","-dev 3 -log"));
		pixelSpinner.setSelection(mPref.getInt("pixelformat", 0));
		resizeWorkaround.setChecked(mPref.getBoolean("enableResizeWorkaround", true));
		resolution.setChecked(mPref.getBoolean("resolution_fixed", false ));
		resWidth.setText(String.valueOf(mPref.getInt("resolution_width",854)));
		resHeight.setText(String.valueOf(mPref.getInt("resolution_height",480)));
		resScale.setText(String.valueOf(mPref.getFloat("resolution_scale",2.0f)));
		if( mPref.getBoolean("resolution_custom", false) )
			radioCustom.setChecked(true);
		else radioScale.setChecked(true);
		if( sdk >= 19 )
		{
			immersiveMode.setChecked(mPref.getBoolean("immersive_mode", true));
		}
		else
		{
			immersiveMode.setVisibility(View.GONE); // not available
		}
				
		resPath.setOnFocusChangeListener( new View.OnFocusChangeListener()
		{
			@Override
			public void onFocusChange(View v, boolean hasFocus)
			{
				updatePath( resPath.getText().toString() );
			}
		} );

		if( !XashConfig.GP_VERSION && // disable autoupdater for Google Play
			mPref.getBoolean("check_updates", true))
		{
			new CheckUpdate(true, updateToBeta.isChecked()).execute(UPDATE_LINK);
		}
		changeButtonsStyle((ViewGroup)tabHost.getParent());

	}

	void updatePath( String text )
	{
		tvResPath.setText(getResources().getString(R.string.text_res_path) + ":\n" + text );
		resPath.setText(text);
	
	}
    public void startXash(View view)
    {
		Intent intent = new Intent(this, XashActivity.class);
		intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);

		SharedPreferences.Editor editor = mPref.edit();
		editor.putString("argv", cmdArgs.getText().toString());
		editor.putBoolean("usevolume",useVolume.isChecked());
		editor.putString("basedir", resPath.getText().toString());
		editor.putInt("pixelformat", pixelSpinner.getSelectedItemPosition());
		editor.putBoolean("enableResizeWorkaround",resizeWorkaround.isChecked());
		editor.putBoolean("check_updates", checkUpdates.isChecked());
		editor.putBoolean("resolution_fixed", resolution.isChecked());
		editor.putBoolean("resolution_custom", radioCustom.isChecked());
		editor.putFloat("resolution_scale", Float.valueOf(resScale.getText().toString()));
		editor.putInt("resolution_width", Integer.valueOf(resWidth.getText().toString()));
		editor.putInt("resolution_height", Integer.valueOf(resHeight.getText().toString()));
		
		

		if( sdk >= 19 )
			editor.putBoolean("immersive_mode", immersiveMode.isChecked());
		else
			editor.putBoolean("immersive_mode", false); // just in case...
		editor.commit();
		startActivity(intent);
    }

	public void aboutXash(View view)
	{
		final Activity a = this;
		this.runOnUiThread(new Runnable() 
		{
			public void run()
			{
				final Dialog dialog = new Dialog(a);
				dialog.setContentView(R.layout.about);
				dialog.setCancelable(true);
				dialog.show();
				TextView tView6 = (TextView) dialog.findViewById(R.id.textView6);
				tView6.setMovementMethod(LinkMovementMethod.getInstance());
			}
		});
	}

	public void selectFolder(View view)
	{
		Intent intent = new Intent(this, in.celest.xash3d.FPicker.class);
		//intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
		//Intent intent = new Intent("android.intent.action.OPEN_DOCUMENT_TREE");
		startActivityForResult(intent, 42);
		resPath.setEnabled(false);
	}

	public void onActivityResult(int requestCode, int resultCode, Intent resultData) 
	{
		if (resultCode == RESULT_OK) 
		{
			try	
			{
				if( resPath == null )
					return;
				updatePath(resultData.getStringExtra("GetPath"));
				resPath.setEnabled(true);
			}
			catch(Exception e)
			{
				e.printStackTrace();
			}
		}
		resPath.setEnabled(true);
	}

	public void createShortcut(View view)
	{
		Intent intent = new Intent(this, ShortcutActivity.class);
		intent.putExtra( "basedir", resPath.getText().toString() );
		intent.putExtra( "name", "Xash3D" );
		intent.putExtra( "argv", cmdArgs.getText().toString() );
		startActivity(intent);
	}

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Inflate the menu; this adds items to the action bar if it is present.
        //getMenuInflater().inflate(R.menu.menu_launcher, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        // Handle action bar item clicks here. The action bar will
        // automatically handle clicks on the Home/Up button, so long
        // as you specify a parent activity in AndroidManifest.xml.
        int id = item.getItemId();

        //noinspection SimplifiableIfStatement
        /*if (id == R.id.action_settings) {
            return true;
        }*/

        return super.onOptionsItemSelected(item);
    }

	private class CheckUpdate extends AsyncTask<String, Void, String> {
		InputStream is = null;
		ByteArrayOutputStream os = null;
		boolean mSilent;
		boolean mBeta;

		public CheckUpdate( boolean silent, boolean beta )
		{
			mSilent = silent;
			mBeta = beta;
		}

		protected String doInBackground(String... urls) 
		{
			try
			{
				URL url = new URL(urls[0]);
				is = url.openConnection().getInputStream();
				os = new ByteArrayOutputStream();

				byte[] buffer = new byte[8196];
				int len;

				while ((len = is.read(buffer)) > 0)
				{
					os.write(buffer, 0, len);
				}
				os.flush();
				
				return os.toString();
			}
			catch(Exception e)
			{
				e.printStackTrace();
				return null;
			}
		}

		protected void onPostExecute(String result)
		{
			JSONArray releases = null;
			try
			{
				if (is != null)
				{
					is.close();
					is = null;
				}
			}
			catch(Exception e)
			{
				e.printStackTrace();
			}

			try
			{
				if (os != null) 
				{
					releases = new JSONArray(os.toString());
					os.close();
					os = null;
				}
			}
			catch(Exception e)
			{
				e.printStackTrace();
				return;
			}
			
			if( releases == null )
				return;
			
			for( int i = 0; i < releases.length(); i++ )
			{
				final JSONObject obj;
				try 
				{
					obj = releases.getJSONObject(i);

					final String version, url, name;
					final boolean beta = obj.getBoolean("prerelease");

					if( beta && !mBeta )
						continue;

					version = obj.getString("tag_name");
					url = obj.getString("html_url");
					name = obj.getString("name");
					Log.d("Xash", "Found: " + version +
						", I: " + getString(R.string.version_string));

					// this is an update
					if( getString(R.string.version_string).compareTo(version) < 0 )
					{
						String dialog_message = String.format(getString(R.string.update_message), name);
						AlertDialog.Builder builder = new AlertDialog.Builder(getBaseContext());
						builder.setMessage(dialog_message)
							.setPositiveButton(R.string.update, new DialogInterface.OnClickListener()
							{
								public void onClick(DialogInterface dialog, int id)
								{
									final Intent intent = new Intent(Intent.ACTION_VIEW).setData(Uri.parse(url));
									startActivity(intent);
								}
							})
							.setNegativeButton(R.string.cancel, new DialogInterface.OnClickListener()
								{ public void onClick(DialogInterface dialog, int id) {} } );
						builder.create().show();
					}
					else if( !mSilent )
					{
						Toast.makeText(getBaseContext(), R.string.no_updates, Toast.LENGTH_SHORT).show();
					}

					// No need to check other releases, so we will stop here.
					break;
				}
				catch(Exception e)
				{
					e.printStackTrace();
					continue;
				}
			}
		}
	}
}
