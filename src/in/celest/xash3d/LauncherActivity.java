package in.celest.xash3d;

import android.app.Activity;
import android.app.Dialog;
import android.app.AlertDialog;

import android.os.AsyncTask;
import android.os.Bundle;
import android.os.Build;
import android.os.Environment;

import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.Window;

import android.content.Intent;
import android.content.ComponentName;
import android.content.Context;
import android.content.pm.PackageManager;
import android.content.SharedPreferences;
import android.content.DialogInterface;
import android.content.DialogInterface.OnDismissListener;

import android.widget.EditText;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.Button;
import android.widget.Spinner;
import android.widget.ArrayAdapter;
import android.widget.TabHost;
import android.widget.ToggleButton;
import android.widget.Toast;

import android.net.Uri;

import android.util.Log;

import java.lang.reflect.Method;

import java.util.List;

import java.io.File;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.FileOutputStream;

import java.net.URLConnection;
import java.net.URL;

import org.json.*;

import in.celest.xash3d.hl.R;

public class LauncherActivity extends Activity {
   // public final static String ARGV = "in.celest.xash3d.MESSAGE";
   	public final static int sdk = Integer.valueOf(Build.VERSION.SDK);
	public final static String UPDATE_LINK = "https://api.github.com/repos/SDLash3D/xash3d-android-project/releases/latest";
	static EditText cmdArgs;
	static ToggleButton useVolume;
	static CheckBox	checkUpdates;
	static EditText resPath;
	static SharedPreferences mPref;
	static Spinner pixelSpinner;
	
	String getDefaultPath()
	{
		File dir = Environment.getExternalStorageDirectory();
		if( dir != null && dir.exists() )
			return dir.getPath() + "/xash";
		return "/sdcard/xash";
	}
    
    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
		super.onCreate(savedInstanceState);
		this.requestWindowFeature(Window.FEATURE_NO_TITLE);
		//super.setTheme( 0x01030005 );
		if ( sdk >= 21 )
		super.setTheme( 0x01030224 );
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

		mPref		 = getSharedPreferences("engine", 0);
		cmdArgs		 = (EditText)findViewById(R.id.cmdArgs);
		useVolume	 = ( ToggleButton ) findViewById( R.id.useVolume );
		resPath		 = ( EditText ) findViewById( R.id.cmdPath );
		checkUpdates = (CheckBox)findViewById(R.id.check_updates);
		pixelSpinner = (Spinner) findViewById(R.id.pixelSpinner);

		final String[] list = {
			"RGBA8888",
			"RGBA888",
			"RGB565",
			"RGBA5551",
			"RGBA4444",
			"RGB332"
		};
		ArrayAdapter<String> adapter = new ArrayAdapter<String>(this,android.R.layout.simple_spinner_item, list);
		adapter.setDropDownViewResource(android.R.layout.simple_spinner_item);
		pixelSpinner.setAdapter(adapter);
		useVolume.setChecked(mPref.getBoolean("usevolume",true));
		checkUpdates.setChecked(mPref.getBoolean("check_updates",true));
		resPath.setText(mPref.getString("basedir", getDefaultPath()));
		cmdArgs.setText(mPref.getString("argv","-dev 3 -log"));
		pixelSpinner.setSelection(mPref.getInt("pixelformat", 0));

		if(mPref.getBoolean("check_updates", true))
		{
			new CheckUpdate(true).execute(UPDATE_LINK);
		}

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
		editor.putBoolean("check_updates", checkUpdates.isChecked());
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
				resPath = ( EditText ) findViewById( R.id.cmdPath );
				resPath.setText( resultData.getStringExtra("GetPath"));

//				final List<String> paths = resultData.getData().getPathSegments();
//				String[] parts = paths.get(1).split(":");
//				String storagepath = Environment.getExternalStorageDirectory().getPath() + "/";
//				String path = storagepath + parts[1];
//				if( path != null)
//					resPath.setText( path );
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

		public CheckUpdate( boolean silent )
		{
			mSilent = silent;
		}

		protected String doInBackground(String... urls) {
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
			JSONObject obj = new JSONObject(os.toString());

			try
			{
				if (os != null) 
				{
					os.close();
					os = null;
				}
			}
			catch(Exception e)
			{
				e.printStackTrace();
			}
			final String version = obj.getString("tag_name");
			final String url 	 = obj.getString("html_url");
			final String name	 = obj.getString("name");

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
						public void onClick(DialogInterface dialog, int id) {
							final Intent intent = new Intent(Intent.ACTION_VIEW).setData(Uri.parse(url));
							startActivity(intent);
						}
					})
					.setNegativeButton(R.string.cancel, new DialogInterface.OnClickListener()
					{
						public void onClick(DialogInterface dialog, int which){}
					});
				builder.create().show();
			}
			else if( !mSilent )
			{
				Toast.makeText(getBaseContext(), R.string.no_updates, Toast.LENGTH_SHORT).show();
			}
		}
	}
}
