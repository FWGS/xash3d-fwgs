package in.celest.xash3d;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public class InstallReceiver extends BroadcastReceiver {
private static final String TAG = "XASH3D";
@Override
public void onReceive(Context context, Intent arg1) {
	Log.d( TAG, "Install received, extracting PAK" );
    org.libsdl.app.SDLActivity.extractPAK( context, true );
    }
}
