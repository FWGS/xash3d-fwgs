package in.celest.xash3d;

import android.app.Activity;
import android.view.View;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import in.celest.xash3d.hl.R;
import android.widget.EditText;
import java.io.File;
import java.io.FilenameFilter;

import android.os.*;

public class ShortcutActivity extends Activity
{
	static EditText name;
	@Override
	protected void onCreate(Bundle bundle)
	{
		super.onCreate(bundle);
		setContentView(R.layout.activity_shortcut);
		name=(EditText)findViewById(R.id.shortcut_name);
		//name.setText("Name");
	}
	public void saveShortcut(View view)
	{
		Intent intent = new Intent();
		intent.setAction("in.celest.xash3d.START");
		EditText argv = (EditText)findViewById(R.id.shortcut_cmdArgs);
		if(argv.length() != 0) intent.putExtra("argv",argv.getText().toString());
		EditText gamedir = (EditText)findViewById(R.id.shortcut_gamedir);
		if(gamedir.length() != 0) intent.putExtra("gamedir",gamedir.getText().toString());
		Intent wrapIntent = new Intent();
		wrapIntent.putExtra(Intent.EXTRA_SHORTCUT_INTENT, intent);
		wrapIntent.putExtra(Intent.EXTRA_SHORTCUT_NAME, name.getText().toString());
		Bitmap icon;
		// Try find icon
		try
		{
			FilenameFilter icoFilter = new FilenameFilter() {
				public boolean accept(File dir, String name) {
					if(name.endsWith(".ico") || name.endsWith(".ICO")) {
						return true;
					}
					return false;
				}
			};
			String gamedirstring = getSharedPreferences("engine", 0).getString("basedir","/sdcard/xash/")+(gamedir.length()!=0?gamedir.getText().toString():"valve");
			File gamedirfile = new File(gamedirstring);
			String files[] = gamedirfile.list(icoFilter);
			int size = (int) getResources().getDimension(android.R.dimen.app_icon_size);
			icon = Bitmap.createScaledBitmap(BitmapFactory.decodeFile(gamedirstring+"/"+files[0]), size, size, false);
		}
		catch(Exception e)
		{
			// Android may not support ico loading, so fallback if something going wrong
			icon = BitmapFactory.decodeResource(getResources(), R.drawable.ic_launcher);
		}
		wrapIntent.putExtra(Intent.EXTRA_SHORTCUT_ICON, icon);
		setResult(RESULT_OK, wrapIntent);
		finish();
	}
}
