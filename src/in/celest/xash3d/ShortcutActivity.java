package in.celest.xash3d;

import android.app.Activity;
import android.view.View;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import in.celest.xash3d.hl.R;
import android.widget.EditText;

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
		name.setText("Name");
	}
	public void saveShortcut(View view)
	{
		Intent intent = new Intent(Intent.ACTION_VIEW,null);
		intent.setType("xash3d.android/game");
		EditText argv = (EditText)findViewById(R.id.shortcut_cmdArgs);
		if(argv.length() != 0) intent.putExtra("argv",argv.getText().toString());
		EditText gamedir = (EditText)findViewById(R.id.shortcut_gamedir);
		if(gamedir.length() != 0) intent.putExtra("gamedir",gamedir.getText().toString());
		Intent wrapIntent = new Intent();
		wrapIntent.putExtra(Intent.EXTRA_SHORTCUT_INTENT, intent);
		wrapIntent.putExtra(Intent.EXTRA_SHORTCUT_NAME, name.getText().toString());
		Bitmap icon = BitmapFactory.decodeResource(getResources(), R.drawable.ic_launcher);
		/// TODO: Load icon from path+gamedir+"game.ico"
		int size = (int) getResources().getDimension(android.R.dimen.app_icon_size);
		wrapIntent.putExtra(Intent.EXTRA_SHORTCUT_ICON, Bitmap.createScaledBitmap(icon, size, size, false));
		setResult(RESULT_OK, wrapIntent);
		finish();
	}
}
