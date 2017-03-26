package in.celest.xash3d;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.opengles.GL10;
import javax.microedition.khronos.egl.*;

import android.app.*;
import android.content.*;
import android.view.*;
import android.os.*;
import android.util.*;
import android.graphics.*;
import android.text.method.*;
import android.text.*;
import android.media.*;
import android.hardware.*;
import android.content.*;
import android.widget.*;
import android.content.pm.*;
import android.net.Uri;
import android.provider.*;
import android.database.*;

import android.view.inputmethod.*;

import java.lang.*;
import java.util.List;
import java.security.MessageDigest;

import in.celest.xash3d.hl.R;
import in.celest.xash3d.hl.BuildConfig;
import in.celest.xash3d.XashConfig;
import in.celest.xash3d.JoystickHandler;
import in.celest.xash3d.CertCheck;


public class XashService  extends Service {
	public static Notification notification;
	public static int status_image = R.id.status_image;
	public static int status_text = R.id.status_text;

	@Override
	public IBinder onBind(Intent intent) {
		return null;
	}
	public static class exitButtonListener extends BroadcastReceiver {
		@Override
		public void onReceive(Context context, Intent intent) {
			XashActivity.nativeUnPause();
			XashActivity.nativeOnDestroy();
		}
	}

	@Override
	public int onStartCommand(Intent intent, int flags, int startId) {

		int status_exit_button = R.id.status_exit_button;
		int notify = R.layout.notify;
		if( XashActivity.sdk >= 21 )
		{
			status_image = R.id.status_image_21;
			status_text = R.id.status_text_21;
			status_exit_button = R.id.status_exit_button_21;
			notify = R.layout.notify_21;
		}
		Log.d("XashService", "Service Started");
		Intent intent_ = new Intent(this, XashActivity.class);
		intent_.setFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
		final PendingIntent pendingIntent = PendingIntent.getActivity(
			getApplicationContext(), 0, intent_, 0);
		notification = new Notification(R.drawable.ic_launcher,
										"Xash3D", System.currentTimeMillis());

		notification.contentView = new RemoteViews(getApplicationContext()
												   .getPackageName(),  notify);
		notification.contentIntent = pendingIntent;
				notification.flags = notification.flags
			| Notification.FLAG_ONGOING_EVENT | Notification.FLAG_FOREGROUND_SERVICE;


		notification.contentView.setTextViewText(status_text, "Xash3D Engine");
	Intent exitIntent = new Intent(this, exitButtonListener.class);
	PendingIntent pendingExitIntent = PendingIntent.getBroadcast(this, 0,
			exitIntent, 0);

		notification.contentView.setOnClickPendingIntent(status_exit_button, pendingExitIntent);
		startForeground(100, notification);
		return START_NOT_STICKY;
	}

	@Override
	public void onDestroy() {
		super.onDestroy();
		Log.d("XashService", "Service Destroyed");
	}

	@Override
	public void onCreate()
	{

	}

	@Override
	public void onTaskRemoved(Intent rootIntent) {
		Log.e("XashService", "OnTaskRemoved");
		if( XashActivity.mEngineReady )
		{
			XashActivity.mEngineReady = false;
			XashActivity.nativeUnPause();
			XashActivity.nativeOnDestroy();
		}
		stopSelf();
	}

};
