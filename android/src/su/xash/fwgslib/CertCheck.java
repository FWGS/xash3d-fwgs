package su.xash.fwgslib;

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

import java.lang.*;
import java.util.List;
import java.security.MessageDigest;

import su.xash.engine.XashConfig; // change pkgname if needed

public class CertCheck
{
	// Certificate checking
	private static String SIG = "DMsE8f5hlR7211D8uehbFpbA0n8=";
	private static String SIG_TEST = ""; // a1ba: mittorn, add your signature later
	
	private static String TAG = "XASH3D:CertCheck";

	public static boolean dumbAntiPDALifeCheck( Context context )
	{
		if( !XashConfig.CHECK_SIGNATURES /* || BuildConfig.DEBUG */ )
			return false; // disable checking for debug builds
			
		final String sig;
		
		if( XashConfig.PKG_TEST )
		{
			sig = SIG_TEST;
		}
		else
		{
			sig = SIG;
		}
	
		if( dumbCertificateCheck( context, context.getPackageName(), sig, false ) )
		{
			Log.e(TAG, "Please, don't resign our public release builds!");
			Log.e(TAG, "If you want to insert some features, rebuild package with ANOTHER package name from git repository.");
			return true;
		}
		
		return false;
	}
	
	public static boolean dumbCertificateCheck( Context context, String pkgName, String sig, boolean failIfNoPkg )
	{
		if( sig == null )
			sig = SIG;
	
		Log.d( TAG, "pkgName = " + pkgName );
		try
		{
			PackageInfo info = context.getPackageManager()
				.getPackageInfo( pkgName, PackageManager.GET_SIGNATURES );
			
			for( Signature signature: info.signatures )
			{
				Log.d( TAG, "found signature" );
				MessageDigest md = MessageDigest.getInstance( "SHA" );
				final byte[] signatureBytes = signature.toByteArray();

				md.update( signatureBytes );

				final String curSIG = Base64.encodeToString( md.digest(), Base64.NO_WRAP );

				if( sig.equals(curSIG) )
				{
					Log.d( TAG, "Found valid cert" );
					return false;
				}
			}
		} 
		catch( PackageManager.NameNotFoundException e )
		{
			Log.d( TAG, "Package not found" );

			e.printStackTrace();
			if( !failIfNoPkg )
				return false;
			
		}
		catch( Exception e ) 
		{
			e.printStackTrace();
		}
		
		return true;
	}
}
