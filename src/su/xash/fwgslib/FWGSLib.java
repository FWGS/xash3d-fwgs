package su.xash.fwgslib;

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
import java.io.*;
import java.net.*;
import org.json.*;
import android.preference.*;

/*
 * This utility class is intended to hide some Android and Java design-flaws and
 * also just shortcuts
 */
public class FWGSLib
{
	private static final String TAG = "FWGSLib";
	static String externalFilesDir;
	public static boolean FBitSet( final int bits, final int mask )
	{
		return ((bits & mask) == mask);
	}
	
	public static float atof( String str, float fallback )
	{
		float ret;
		try
		{
			ret = Float.valueOf( str );
		}
		catch( Exception e )
		{
			ret = fallback;
		}
		
		return ret;
	}
	
	public static int atoi( String str, int fallback )
	{
		int ret;
		try
		{
			ret = Integer.valueOf( str );
		}
		catch( Exception e )
		{
			ret = fallback;
		}
		
		return ret;
	}
	
	public static boolean checkGameLibDir( String gamelibdir, String allowed )
	{
		try
		{
			Log.d( TAG, " gamelibdir = " + gamelibdir + " allowed = " + allowed );
			
			if( gamelibdir.contains( "/.." ))
				return false;
			
			File f = new File( gamelibdir );
		
			if( !f.isDirectory() )
			{
				Log.d( TAG, "Not a directory" );
				return false;
			}
		
			if( !f.exists() )
			{
				Log.d( TAG, "Does not exist" );
				return false;
			}
			
			// add trailing / for simple regexp
			if( gamelibdir.charAt(gamelibdir.length() - 1) != '/' )
				gamelibdir = gamelibdir + "/";
					
			final String regex = ".+\\/" + allowed.replace(".",  "\\.") + "(|(-\\d))\\/(.+|)";
			
			Log.d( TAG, regex );
		
			final boolean ret =  gamelibdir.matches( regex );
			
			Log.d( TAG, "ret = " + ret );
			
			return ret;
		}
		catch( Exception e )
		{
			e.printStackTrace();
		}
		return false;
	}
	
	public static String getDefaultXashPath()
	{
		File dir = Environment.getExternalStorageDirectory();
		if( dir != null && dir.exists() )
			return dir.getPath() + "/xash";
		return "/sdcard/xash";
	}
	static class GetExternalFilesDir extends Thread
	{
		Context ctx;
		GetExternalFilesDir( Context ctx1 )
		{
			ctx = ctx1;
		}
		@Override
		public void run()
		{
			try
			{
				File f = ctx.getExternalFilesDir(null);

				f.mkdirs();

		 		externalFilesDir = f.getAbsolutePath();
				Log.d(TAG, "getExternalFilesDir success");
			}
			catch( Exception e )
			{
				Log.e( TAG, e.toString(), e);
			}
		}
	}
	public static String getExternalFilesDir( Context ctx )
	{
		if( externalFilesDir != null )
			return externalFilesDir;
		try
		{
			if( sdk >= 8 )
			{
				Thread t = new GetExternalFilesDir(ctx);
				t.start();
				t.join(2000);
			}
		}
		catch(Exception e)
		{
			Log.e( TAG, e.toString(), e);
			externalFilesDir = getDefaultXashPath();
		}
		if( externalFilesDir == null )
			externalFilesDir = getDefaultXashPath();
		return externalFilesDir;
	}
	
	public static boolean isLandscapeOrientation( Activity act )
	{
		DisplayMetrics metrics = new DisplayMetrics();
		act.getWindowManager().getDefaultDisplay().getMetrics(metrics);
		return (metrics.widthPixels > metrics.heightPixels);
	}
	
	public static String getStringExtraFromIntent( Intent intent, String extraString, String ifNotFound )
	{
		String ret = intent.getStringExtra( extraString );
		if( ret == null )
		{
			ret = ifNotFound;
		}
		
		return ret;
	}

	
	public static final int sdk = Integer.valueOf(Build.VERSION.SDK);
}
