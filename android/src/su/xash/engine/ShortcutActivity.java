package su.xash.engine;

import android.app.Activity;
import android.view.View;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.widget.Toast;
import su.xash.engine.R;
import android.widget.EditText;
import android.widget.Button;
import java.io.File;
import java.io.FilenameFilter;

import android.os.*;

public class ShortcutActivity extends Activity
{
	static EditText name, gamedir, pkgname, argv;
	String [] env = null;
	public static final int sdk = Integer.valueOf(Build.VERSION.SDK);
	@Override
	protected void onCreate(Bundle bundle)
	{
		super.onCreate(bundle);
		//material dialog
		if ( sdk >= 21 )
			super.setTheme( 0x01030225 );
		else super.setTheme( 0x0103000b );
		setContentView(R.layout.activity_shortcut);
		Intent intent=getIntent();
		name = (EditText)findViewById(R.id.shortcut_name);
		pkgname = (EditText)findViewById(R.id.shortcut_pkgname);
		gamedir = (EditText)findViewById(R.id.shortcut_gamedir);
		argv = (EditText)findViewById(R.id.shortcut_cmdArgs);
		((Button)findViewById( R.id.shortcut_buttonOk )).setOnClickListener(new View.OnClickListener(){
           	   @Override
            public void onClick(View v) {
			saveShortcut(v);
                }
		});
		String argvs = intent.getStringExtra("argv");
		if( argvs != null )
			argv.setText(argvs);
		String pkgnames = intent.getStringExtra("pkgname");
		if( pkgnames != null )
			pkgname.setText(pkgnames);
		String gamedirs = intent.getStringExtra("gamedir");
		if( gamedirs != null )
			gamedir.setText(gamedirs);
		String names = intent.getStringExtra("name");
		if( names != null )
			name.setText(names);
		env = intent.getStringArrayExtra("env");

		//name.setText("Name");
	}
	public void saveShortcut(View view)
	{
		Intent intent = new Intent();
		intent.setAction("su.xash.engine.START");
		if(argv.length() != 0) intent.putExtra("argv",argv.getText().toString());
		if(pkgname.length() != 0)
		{
			intent.putExtra("gamelibdir", "/data/data/"+pkgname.getText().toString().replace("!","in.celest.xash3d.")+"/lib/");
			intent.putExtra("pakfile", "/data/data/"+pkgname.getText().toString().replace("!","in.celest.xash3d.")+"/files/extras.pak");
		}
		if(gamedir.length() != 0) intent.putExtra("gamedir",gamedir.getText().toString());
		if(env != null)
			 intent.putExtra("env", env);
		Intent wrapIntent = new Intent();
		wrapIntent.putExtra(Intent.EXTRA_SHORTCUT_INTENT, intent);
		wrapIntent.putExtra(Intent.EXTRA_SHORTCUT_NAME, name.getText().toString());

		Bitmap icon = null;
		// Try find icon
		int size = (int) getResources().getDimension(android.R.dimen.app_icon_size);
		String gamedirstring = getSharedPreferences("engine", 0).getString("basedir","/sdcard/xash/")+"/"+(gamedir.length()!=0?gamedir.getText().toString():"valve");
		try
		{
			icon = Bitmap.createScaledBitmap(BitmapFactory.decodeFile(gamedirstring+"/icon.png"), size, size, false);
		}
		catch(Exception e)
		{
		}
		if(icon == null) try
		{
			icon = Bitmap.createScaledBitmap(BitmapFactory.decodeFile(gamedirstring+"/game.ico"), size, size, false);
		}
		catch(Exception e)
		{
		}
		if(icon == null) try
		{
			FilenameFilter icoFilter = new FilenameFilter() {
				public boolean accept(File dir, String name) {
					if(name.endsWith(".ico") || name.endsWith(".ICO")) {
						return true;
					}
					return false;
				}
			};

			File gamedirfile = new File(gamedirstring);
			String files[] = gamedirfile.list(icoFilter);
			icon = Bitmap.createScaledBitmap(BitmapFactory.decodeFile(gamedirstring+"/"+files[0]), size, size, false);
		}
		catch(Exception e)
		{
			// Android may not support ico loading, so fallback if something going wrong
			icon = BitmapFactory.decodeResource(getResources(), R.drawable.ic_launcher);
		}
		wrapIntent.putExtra(Intent.EXTRA_SHORTCUT_ICON, icon);
		if(getIntent().getAction() == "android.intent.action.CREATE_SHORTCUT" ) // Called from launcher
		{
			setResult(RESULT_OK, wrapIntent);
			finish();
		}
		else try
		{
			wrapIntent.setAction("com.android.launcher.action.INSTALL_SHORTCUT");
			getApplicationContext().sendBroadcast(wrapIntent);
			Toast.makeText(getApplicationContext(), "Shortcut created!", Toast.LENGTH_SHORT).show();
		}
		catch(Exception e)
		{
			Toast.makeText(getApplicationContext(), "Problem creating shortcut: " + e.toString() +
				"\nTry create it manually from laucnher", Toast.LENGTH_LONG).show();
		}
	}
}
