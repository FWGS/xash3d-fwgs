package in.celest.xash3d;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Bundle;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.content.Intent;
import android.widget.EditText;
import android.widget.ScrollView;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.LinearLayout;
import android.widget.LinearLayout.LayoutParams;
import android.widget.Button;
import android.widget.TextView;
import android.widget.ArrayAdapter;
import android.widget.AdapterView;
import android.widget.Spinner;
import android.util.Log;
//import android.content.Context;
import android.content.ComponentName;
import android.content.pm.PackageManager;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import android.content.DialogInterface;
import java.io.BufferedReader;
import java.io.BufferedInputStream;
import java.io.InputStreamReader;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.File;
import java.io.FileOutputStream;
import android.os.AsyncTask;
import java.util.List;
import java.util.ArrayList;

import android.text.method.*;
import android.widget.*;



public class SteamActivity extends Activity {
	LinearLayout output;
	ScrollView scroll;
	boolean isScrolling;
	static SteamActivity mSingleton = null;
	static final int[] waitSwitch = new int[1];
	ProgressBar progress;
	TextView progressLine;
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		mSingleton = this;
		// Build layout
		LinearLayout launcher = new LinearLayout(this);
		launcher.setOrientation(LinearLayout.VERTICAL);
		launcher.setLayoutParams(new LayoutParams(LayoutParams.FILL_PARENT, LayoutParams.FILL_PARENT));
		output = new LinearLayout(this);
		output.setLayoutParams(new LayoutParams(LayoutParams.FILL_PARENT, LayoutParams.WRAP_CONTENT));
		output.setOrientation(LinearLayout.VERTICAL);

		scroll = new ScrollView(this);

		// Set launch button title here
		Button startButton = new Button(this);
		startButton.setText("Start");
		LayoutParams buttonParams = new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
		buttonParams.gravity = 5;
		startButton.setLayoutParams(buttonParams);
		startButton.setOnClickListener( new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				if( SteamService.mSingleton == null )
					startService(new Intent(SteamActivity.this, SteamService.class));
			}
		});
		Button stopButton = new Button(this);
		stopButton.setText("Stop");
		LayoutParams stopbuttonParams = new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
		stopbuttonParams.gravity = 5;
		stopButton.setLayoutParams(stopbuttonParams);
		stopButton.setOnClickListener( new View.OnClickListener() {
				@Override
				public void onClick(View v) {
					if( SteamService.mSingleton != null )
						SteamService.mSingleton.cancelThread();
				}
			});
		
		LinearLayout buttons = new LinearLayout(this);
		buttons.setLayoutParams(new LayoutParams(LayoutParams.FILL_PARENT, LayoutParams.WRAP_CONTENT));
		buttons.addView(startButton);
		buttons.addView(stopButton);
		LinearLayout progressLayout = new LinearLayout(this);
		progressLayout.setOrientation(LinearLayout.VERTICAL);
		progressLayout.setLayoutParams(new LayoutParams(LayoutParams.FILL_PARENT, LayoutParams.FILL_PARENT));
		progress = new ProgressBar(this, null,
								   android.R.attr.progressBarStyleHorizontal);
		progress.setMax(100);
		progress.setVisibility(View.GONE);
		progressLine = new TextView(this);
		progressLine.setVisibility(View.GONE);
		progressLine.setTextAppearance(this, android.R.style.TextAppearance_Medium);
		progressLayout.addView( progressLine );
		progressLayout.addView(progress);
		buttons.addView(progressLayout);
		launcher.addView(buttons);
		scroll.addView(output);
		launcher.addView(scroll);
		setContentView(launcher);
		try
		{
			synchronized( waitSwitch )
			{
				waitSwitch.notify();
			}
		}
		catch( Exception e ) {}
	}
	public void progressUpdate(  String str, int p)
	{
		runOnUiThread(new ProgressCallback( str, p ));
	}

	
	public void printText(String str)
	{
		runOnUiThread(new OutputCallback( str ));
	}

	String promptDialog(final String title, final String prompt, final boolean passwd)
	{
		final String[] result = new String[1];
		runOnUiThread(new Runnable()
			{
				@Override
				public void run()
				{
					final EditText edit = new EditText(mSingleton);
					if( passwd )
						edit.setTransformationMethod(new PasswordTransformationMethod());
					new AlertDialog.Builder(mSingleton)
						.setTitle(title)
						.setMessage(prompt)
						.setView(edit)
						.setPositiveButton("Ok", new DialogInterface.OnClickListener() {
							public void onClick(DialogInterface dialog, int whichButton) {
								synchronized(result)
								{
									result[0] = edit.getText().toString();
									result.notify();
								}
							}
						})
						.setNegativeButton("Cancel", new DialogInterface.OnClickListener() {
							public void onClick(DialogInterface dialog, int whichButton) {
								synchronized(result)
								{
									result[0] = null;
									result.notify();
								}
							}
						})
						.setCancelable(false)
						.show();

				}
			});
		synchronized(result)
		{
			try{
				result.wait();
				return result[0];
			}
			catch(Exception e)
			{
				runOnUiThread(new OutputCallback(e.getMessage()));
				return result[0];
			}
		}
	}

	// Callbacks to interact with UI from other threads
	class OutputCallback implements Runnable {
		String str;
		OutputCallback(String s) { str = s; }
		public void run() {
			TextView line = new TextView(SteamActivity.this);
			line.setText(str);

			line.setTextAppearance(SteamActivity.this, android.R.style.TextAppearance_Small);
			line.setLayoutParams(new LayoutParams(LayoutParams.FILL_PARENT, LayoutParams.WRAP_CONTENT));
			progress.setVisibility(View.GONE);
			progressLine.setVisibility(View.GONE);
			if(output.getChildCount() > 256)
				output.removeViewAt(0);
			output.addView(line);
			if( !isScrolling )
				scroll.postDelayed(new Runnable() {
						@Override
						public void run() {
							scroll.fullScroll(ScrollView.FOCUS_DOWN);
							isScrolling = false;
						}
					}, 200);
			isScrolling = true;
		}
	}
	class ProgressCallback implements Runnable {
		String str;
		int p;
		ProgressCallback(String s, int pr) { str = s; p = pr; }
		public void run() {

			progressLine.setText( str );
			progressLine.setVisibility(View.VISIBLE);
			progress.setProgress(p);
			progress.setVisibility(View.VISIBLE);
		}
	}
	@Override
	protected void onDestroy()
	{
		mSingleton = null;
		super.onDestroy();
	}
	
	
}
