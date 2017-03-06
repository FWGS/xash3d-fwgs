 package in.celest.xash3d;

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

import in.celest.xash3d.hl.BuildConfig;
import in.celest.xash3d.XashConfig;

public class CertCheck
{
	// Certificate checking
	private static String SIG = "DMsE8f5hlR7211D8uehbFpbA0n8=";
	private static String SIG_TEST = ""; // a1ba: mittorn, add your signature later
	
	private static String TAG = "XASH3D:CertCheck";

	public static boolean dumbAntiPDALifeCheck( Context context )
	{
		if( BuildConfig.DEBUG || 
			!XashConfig.CHECK_SIGNATURES )
			return false; // disable checking for debug builds
	
		try
		{
			PackageInfo info = context.getPackageManager()
				.getPackageInfo( context.getPackageName(), PackageManager.GET_SIGNATURES );
			
			for( Signature signature: info.signatures )
			{
				MessageDigest md = MessageDigest.getInstance( "SHA" );
				final byte[] signatureBytes = signature.toByteArray();

				md.update( signatureBytes );

				final String curSIG = Base64.encodeToString( md.digest(), Base64.NO_WRAP );

				if( XashConfig.PKG_TEST )
				{
					if( SIG_TEST.equals(curSIG) )
						return false;
				}
				else
				{
					if( SIG.equals(curSIG) )
						return false;
				}
			}
		} 
		catch( Exception e ) 
		{
			e.printStackTrace();
		}
		
		Log.e(TAG, "Please, don't resign our public release builds!");
		Log.e(TAG, "If you want to insert some features, rebuild package with ANOTHER package name from git repository.");
		return true;
	}
}
