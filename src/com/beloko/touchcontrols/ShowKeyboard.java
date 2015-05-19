package com.beloko.touchcontrols;

import android.app.Activity;
import android.content.Context;
import android.content.res.Configuration;
import android.util.Log;
import android.view.View;
import android.view.inputmethod.InputMethodManager;

public class ShowKeyboard {
	static Activity activity;
	static View view;;

	public static void setup(Activity a,View v)
	{
		activity = a;
		view = v;
	}


	public static void toggleKeyboard()
	{
		Log.d("ShowKeyboard","toggleKeyboard");

		InputMethodManager im = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
		if (im != null)
		{
			Log.d("ShowKeyboard","toggleKeyboard...");
			im.toggleSoftInput(0, 0);
		}
	}

	public static void showKeyboard(int show)
	{
		Log.d("ShowKeyboard","showKeyboard " + show);

		InputMethodManager im = (InputMethodManager)activity.getSystemService(Context.INPUT_METHOD_SERVICE);
		if (im != null)
		{
			if (show == 0)
			{
				im.hideSoftInputFromWindow(activity.getCurrentFocus().getWindowToken(), 0);
			}
			if (show == 1)
				if (!im.isAcceptingText()) 
					toggleKeyboard();
			if (show == 2)
				toggleKeyboard();
		}

		/*
		 InputMethodManager imm = (InputMethodManager)activity.getSystemService(Context.INPUT_METHOD_SERVICE);
		 if (show == 1)
			 imm.showSoftInput(view, InputMethodManager.SHOW_FORCED);
		 else
			 imm.hideSoftInputFromWindow(view.getWindowToken(), InputMethodManager.HIDE_NOT_ALWAYS);
		 */
	}
	
	public static boolean hasHardwareKeyboard()
	{
		if(activity == null)
			return false;

		return activity.getApplicationContext().getResources().getConfiguration().keyboard == Configuration.KEYBOARD_QWERTY;
	}

}
