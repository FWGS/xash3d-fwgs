package in.celest.xash3d;

import android.app.Activity;
import android.app.Dialog;
import android.os.Bundle;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.content.Intent;
import android.widget.EditText;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.content.ComponentName;
import android.content.pm.PackageManager;
import android.content.SharedPreferences;
import android.content.DialogInterface;
import android.content.DialogInterface.OnDismissListener;

import in.celest.xash3d.hl.R;
import com.beloko.touchcontrols.TouchControlsSettings;

public class LauncherActivity extends Activity {
   // public final static String ARGV = "in.celest.xash3d.MESSAGE";
	static TouchControlsSettings mSettings;
	static EditText cmdArgs;
	static CheckBox useControls;
	static EditText resPath;
	static SharedPreferences mPref;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_launcher);
		mSettings=new TouchControlsSettings();
		mSettings.setup(this, null);
		mSettings.loadSettings(this);
		mPref = getSharedPreferences("engine", 0);
		cmdArgs = (EditText)findViewById(R.id.cmdArgs);
		cmdArgs.setText(mPref.getString("argv","-dev 3 -log"));
		useControls = ( CheckBox ) findViewById( R.id.useControls );
		useControls.setChecked(mPref.getBoolean("controls",true));
		resPath = ( EditText ) findViewById( R.id.cmdPath );
		resPath.setText(mPref.getString("basedir","/sdcard/xash/"));
		}
    
    public void startXash(View view)
    {
	Intent intent = new Intent(this, org.libsdl.app.SDLActivity.class);
	intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);

	SharedPreferences.Editor editor = mPref.edit();
	editor.putString("argv", cmdArgs.getText().toString());
	editor.putBoolean("controls",useControls.isChecked());
	editor.putString("basedir", resPath.getText().toString());
	editor.commit();
	editor.apply();
	startActivity(intent);
    }
	public void controlsSettings(View view)
	{
		mSettings.loadSettings(this);
		mSettings.showSettings();
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
	
	public void createShortcut(View view)
	{
		Intent intent = new Intent(this, ShortcutActivity.class);
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
