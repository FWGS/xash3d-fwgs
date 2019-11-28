package su.xash.fwgslib;


import android.app.*;
import android.content.*;
import android.net.*;
import android.os.*;
import android.util.*;
import android.widget.*;
import su.xash.engine.*;
import java.io.*;
import java.net.*;
import org.json.*;


public class CheckUpdate extends AsyncTask<String, Void, String> {
	InputStream is = null;
	ByteArrayOutputStream os = null;
	boolean mSilent;
	boolean mBeta;
	Context mContext;

	public CheckUpdate( Context context, boolean silent, boolean beta )
	{
		mSilent = silent;
		mBeta = beta;
		mContext = context;
	}

	protected String doInBackground(String... urls) 
	{
		try
		{
			URL url = new URL(urls[0]);
			is = url.openConnection().getInputStream();
			os = new ByteArrayOutputStream();

			byte[] buffer = new byte[8196];
			int len;

			while ((len = is.read(buffer)) > 0)
			{
				os.write(buffer, 0, len);
			}
			os.flush();

			return os.toString();
		}
		catch(Exception e)
		{
			e.printStackTrace();
			return null;
		}
	}

	protected void onPostExecute(String result)
	{
		JSONArray releases = null;
		try
		{
			if (is != null)
			{
				is.close();
				is = null;
			}
		}
		catch(Exception e)
		{
			e.printStackTrace();
		}

		try
		{
			if (os != null) 
			{
				releases = new JSONArray(os.toString());
				os.close();
				os = null;
			}
		}
		catch(Exception e)
		{
			e.printStackTrace();
			return;
		}

		if( releases == null )
			return;

		for( int i = 0; i < releases.length(); i++ )
		{
			final JSONObject obj;
			try 
			{
				obj = releases.getJSONObject(i);

				final String version, url, name;
				final boolean beta = obj.getBoolean("prerelease");

				if( beta && !mBeta )
					continue;

				version = obj.getString("tag_name");
				url = obj.getString("html_url");
				name = obj.getString("name");
				Log.d("Xash", "Found: " + version +
					  ", I: " + mContext.getString(R.string.version_string));

				// this is an update
				if( mContext.getString(R.string.version_string).compareTo(version) < 0 )
				{
					String dialog_message = String.format(mContext.getString(R.string.update_message), name);
					AlertDialog.Builder builder = new AlertDialog.Builder(mContext);
					builder.setMessage(dialog_message)
						.setPositiveButton(R.string.update, new DialogInterface.OnClickListener()
						{
							public void onClick(DialogInterface dialog, int id)
							{
								final Intent intent = new Intent(Intent.ACTION_VIEW).setData(Uri.parse(url));
								mContext.startActivity(intent);
							}
						})
						.setNegativeButton(R.string.cancel, new DialogInterface.OnClickListener()
						{ public void onClick(DialogInterface dialog, int id) {} } );
					builder.create().show();
				}
				else if( !mSilent )
				{
					Toast.makeText(mContext, R.string.no_updates, Toast.LENGTH_SHORT).show();
				}

				// No need to check other releases, so we will stop here.
				break;
			}
			catch(Exception e)
			{
				e.printStackTrace();
				continue;
			}
		}
	}
}
