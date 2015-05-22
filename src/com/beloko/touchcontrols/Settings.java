package com.beloko.touchcontrols;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import android.util.Log;


public class Settings {
	public static boolean DEBUG = false;
	
	public static String gamePadControlsFile = "gamepadSettings.dat";
	public static String graphicsDir = "";
	
	public static boolean gamePadEnabled;
	public static boolean hideTouchControls;
	
	public enum IDGame{Quake,Quake2,Doom,Duke3d,Quake3,Hexen2,RTCW,Wolf3d,JK2,JK3,Heretic,Hexen,Strife,AVP,Shadow,Gish,Descent1,Descent2,Homeworld,BlakeStone,Noah,Doom3};
	
	public static IDGame game;
	
	
	public static float getFloatOption(Context ctx,String name, float def)
	{
		SharedPreferences settings = ctx.getSharedPreferences("OPTIONS", 	Context.MODE_MULTI_PROCESS);
		return settings.getFloat(name, def);
	}

	public static void setFloatOption(Context ctx,String name, float value)
	{
		SharedPreferences settings = ctx.getSharedPreferences("OPTIONS", 	Context.MODE_MULTI_PROCESS);
		SharedPreferences.Editor editor = settings.edit();
		editor.putFloat(name, value);
		editor.commit();
	}

	public static boolean getBoolOption(Context ctx,String name, boolean def)
	{
		SharedPreferences settings = ctx.getSharedPreferences("OPTIONS", 	Context.MODE_MULTI_PROCESS);
		return settings.getBoolean(name, def);
	}

	public static void setBoolOption(Context ctx,String name, boolean value)
	{
		SharedPreferences settings = ctx.getSharedPreferences("OPTIONS", 	Context.MODE_MULTI_PROCESS);
		SharedPreferences.Editor editor = settings.edit();
		editor.putBoolean(name, value);
		editor.commit();
	}

	public static int getIntOption(Context ctx,String name, int def)
	{
		SharedPreferences settings = ctx.getSharedPreferences("OPTIONS", 	Context.MODE_MULTI_PROCESS);
		return settings.getInt(name, def);
	}

	public static void setIntOption(Context ctx,String name, int value)
	{
		SharedPreferences settings = ctx.getSharedPreferences("OPTIONS", 	Context.MODE_MULTI_PROCESS);
		SharedPreferences.Editor editor = settings.edit();
		editor.putInt(name, value);
		editor.commit();
	}

	public static long getLongOption(Context ctx,String name, long def)
	{
		SharedPreferences settings = ctx.getSharedPreferences("OPTIONS", 	Context.MODE_MULTI_PROCESS);
		return settings.getLong(name, def);
	}

	public static void setLongOption(Context ctx,String name, long value)
	{
		SharedPreferences settings = ctx.getSharedPreferences("OPTIONS", 	Context.MODE_MULTI_PROCESS);
		SharedPreferences.Editor editor = settings.edit();
		editor.putLong(name, value);
		editor.commit();
	}

	public static String getStringOption(Context ctx,String name, String def)
	{
		SharedPreferences settings = ctx.getSharedPreferences("OPTIONS", 	Context.MODE_MULTI_PROCESS);
		return settings.getString(name, def);
	}

	public static void setStringOption(Context ctx,String name, String value)
	{
		SharedPreferences settings = ctx.getSharedPreferences("OPTIONS", 	Context.MODE_MULTI_PROCESS);
		SharedPreferences.Editor editor = settings.edit();
		editor.putString(name, value);
		editor.commit();
	}
	
	static public void copyFile(InputStream in, OutputStream out) throws IOException {
		byte[] buffer = new byte[1024];
		int read;
		while((read = in.read(buffer)) != -1){
			out.write(buffer, 0, read);
		}
		out.close(); 
	}
	
	static public void copyPNGAssets(Context ctx,String dir,String prefix) {

		if (prefix == null)
			prefix = "";

		File d = new File(dir);
		if (!d.exists())
			d.mkdirs();

		AssetManager assetManager = ctx.getAssets();
		String[] files = null;
		try {
			files = assetManager.list("");
		} catch (IOException e) {
			Log.e("tag", "Failed to get asset file list.", e);
		}
		for(String filename : files) {
			if (filename.endsWith("png") && filename.startsWith(prefix)){
				InputStream in = null;
				OutputStream out = null;
				//Log.d("test","file = " + filename);
				try {
					in = assetManager.open(filename);
					out = new FileOutputStream(dir + "/" + filename.substring(prefix.length()));
					copyFile(in, out);
					in.close();
					in = null;
					out.flush();
					out.close();
					out = null;
				} catch(IOException e) {
					Log.e("tag", "Failed to copy asset file: " + filename, e);
				}       
			}
		}
	}
}
