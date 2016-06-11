package in.celest.xash3d;

import android.app.Activity;
import android.app.Dialog;
import android.os.Bundle;
import android.os.Build;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.content.Intent;
import android.view.Window;
import android.widget.EditText;
import android.widget.TextView;

import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.Button;
import android.widget.Spinner;
import android.widget.ArrayAdapter;

import android.content.ComponentName;
import android.content.Context;
import android.content.pm.PackageManager;
import android.content.SharedPreferences;
import android.content.DialogInterface;
import android.content.DialogInterface.OnDismissListener;
import android.net.Uri;
import android.os.Environment;
import java.lang.reflect.Method;
import java.util.List;
import java.io.File;
import android.widget.TabHost;
import android.widget.ToggleButton;

import in.celest.xash3d.hl.R;

public class LauncherActivity extends Activity {
   // public final static String ARGV = "in.celest.xash3d.MESSAGE";
   	public final static int sdk = Integer.valueOf(Build.VERSION.SDK);
	static EditText cmdArgs;
	static ToggleButton useVolume;
	static EditText resPath;
	static SharedPreferences mPref;
	static Spinner pixelSpinner;
	static TextView tvResPath;
	String getDefaultPath()
	{
		File dir = Environment.getExternalStorageDirectory();
		if( dir != null && dir.exists() )
			return dir.getPath() + "/xash";
		return "/sdcard/xash";
	}
    @Override
    protected void onCreate(Bundle savedInstanceState) {
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

	final String[] list = {
	"32 bit (RGBA8888)",
	"24 bit (RGB888)",
	"16 bit (RGB565)",
	"15 bit (RGBA5551)",
	"12 bit (RGBA4444)",
	"8 bit (RGB332)"
	};
	pixelSpinner = (Spinner) findViewById(R.id.pixelSpinner);
	ArrayAdapter<String> adapter = new ArrayAdapter<String>(this,android.R.layout.simple_spinner_item, list);
	//ArrayAdapter<CharSequence> adapter = ArrayAdapter.createFromResource(this, list, android.R.layout.simple_spinner_item);
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
//		if ( Build.VERSION.SDK_INT < 21 )
//			selectFolder.setVisibility( View.GONE );
	mPref = getSharedPreferences("engine", 0);
	cmdArgs = (EditText)findViewById(R.id.cmdArgs);
	cmdArgs.setText(mPref.getString("argv","-dev 3 -log"));
	useVolume = ( ToggleButton ) findViewById( R.id.useVolume );
	useVolume.setChecked(mPref.getBoolean("usevolume",true));
	resPath = ( EditText ) findViewById( R.id.cmdPath );
	tvResPath = ( TextView ) findViewById( R.id.textView_path );
	updatePath(mPref.getString("basedir", getDefaultPath()));
	resPath.setOnFocusChangeListener( new View.OnFocusChangeListener()
	
	{
		@Override
		public void onFocusChange(View v, boolean hasFocus)
		{
			updatePath( resPath.getText().toString() );
		}
	} );
	pixelSpinner.setSelection(mPref.getInt("pixelformat", 0));
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
	editor.commit();
	startActivity(intent);
    }

	public void aboutXash(View view)
	{
		final Activity a = this;
		this.runOnUiThread(new Runnable() {
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
	//	Intent intent = new Intent("android.intent.action.OPEN_DOCUMENT_TREE");
		startActivityForResult(intent, 42);
		resPath.setEnabled(false);
	}

public void onActivityResult(int requestCode, int resultCode, Intent resultData) {
		if (resultCode == RESULT_OK) {
try{

		if( resPath == null )
		    return;
		updatePath( resultData.getStringExtra("GetPath"));

//	final List<String> paths = resultData.getData().getPathSegments();
//			String[] parts = paths.get(1).split(":");
//			String storagepath = Environment.getExternalStorageDirectory().getPath() + "/";
//			String path = storagepath + parts[1];
//			if( path != null)
//				resPath.setText( path );
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
}
