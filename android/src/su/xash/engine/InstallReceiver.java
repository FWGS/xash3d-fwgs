package su.xash.engine;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;
import java.io.FileOutputStream;
import java.io.InputStream;
import android.content.SharedPreferences;

public class InstallReceiver extends BroadcastReceiver {
	private static final String TAG = "XASH3D";
	@Override
	public void onReceive(Context context, Intent arg1) {
		String pkgname = arg1.getData().getEncodedSchemeSpecificPart();
		Log.d( TAG, "Install received, package " + pkgname );
    		if( context.getPackageName().equals(pkgname) )
    			extractPAK(context, true);
	}
	public static SharedPreferences mPref = null;
	private static final int PAK_VERSION = 7;
	public static synchronized void extractPAK(Context context, Boolean force) {
		InputStream is = null;
		FileOutputStream os = null;
		try {
			if( mPref == null )
				mPref = context.getSharedPreferences("engine", 0);
			synchronized( mPref )
			{
				if( mPref.getInt( "pakversion", 0 ) == PAK_VERSION && !force )
					return;
				String path = context.getFilesDir().getPath()+"/extras.pak";

				is = context.getAssets().open("extras.pak");
				os = new FileOutputStream(path);
				byte[] buffer = new byte[1024];
				int length;
				while ((length = is.read(buffer)) > 0) {
					os.write(buffer, 0, length);
				}
				os.close();
				is.close();
				SharedPreferences.Editor editor = mPref.edit();
				editor.putInt( "pakversion", PAK_VERSION );
				editor.commit();
			}
		} catch( Exception e )
		{
			Log.e( TAG, "Failed to extract PAK:" + e.toString() );
		}
	}
}
