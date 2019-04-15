package in.celest.xash3d;


import android.content.*;
import android.os.IBinder;
import android.util.Log;
import android.widget.*;
import android.app.*;
import android.view.View;
import java.io.*;
import java.util.*;
import java.net.URL;
import java.net.URLConnection;
import in.celest.xash3d.hl.R;
import android.widget.RemoteViews.*;
import android.util.*;
import java.util.concurrent.atomic.*;


enum ProcessState{ UNPACK, LAUNCH, COMMAND, WAIT, DOWNLOAD };

public class SteamService extends Service
{
	final static String TAG = "SteamService";
	public static SteamService mSingleton;
	private Notification notification;
	private NotificationManager notificationManager = null;
	private String localPath;
	private String filesDir;
	class RestartException extends Exception {};
	class CancelException extends Exception {};
	

	static BackgroundThread mBgThread;
	private SharedPreferences mPref;
	public void onCreate() {
		super.onCreate();
		mSingleton = this;
		mPref = getSharedPreferences("steam", 0);
		synchronized(mPref)
		{
			// test
			mPref.edit()
				.putStringSet("pending_verify", new HashSet<String>(Arrays.asList("70")))
				.putStringSet("pending_download", new HashSet<String>(Arrays.asList("70")))
				.commit();
		}
		Log.d(TAG, "onCreate");
	}

	private void notificationInit()
	{
		// init notification and foreground service
		Intent intent = new Intent(this, SteamActivity.class);
        final PendingIntent pendingIntent = PendingIntent.getActivity(
			getApplicationContext(), 0, intent, 0);
        notification = new Notification(R.drawable.ic_launcher,
										"SteamCMD download", System.currentTimeMillis());
        notification.flags = notification.flags
			| Notification.FLAG_ONGOING_EVENT | Notification.FLAG_FOREGROUND_SERVICE;
        notification.contentView = new RemoteViews(getApplicationContext()
												   .getPackageName(),  R.layout.notify);

		notification.contentView.setViewVisibility( R.id.status_progress, View.GONE );
        notification.contentIntent = pendingIntent;		
		if( notificationManager == null )
			notificationManager = (NotificationManager) getApplicationContext()
				.getSystemService(Context.NOTIFICATION_SERVICE);
		startForeground(100, notification);
	}
	
	// Dont create too much notification intents, it may crash system
	long lastNotify = 0;
	private void progressUpdate( String text, int progress) {
		if( SteamActivity.mSingleton != null )
			SteamActivity.mSingleton.progressUpdate( text, progress );
		if( notification == null )
			notificationInit();
		long notify = System.currentTimeMillis();
		// allow 1s interval
		if( notify - lastNotify < 999 )
			return;
		lastNotify = notify;
        notification.contentView.setTextViewText(R.id.status_text, text);
        notification.contentView.setProgressBar(R.id.status_progress, 100, progress, false);
		notification.contentView.setViewVisibility( R.id.status_progress, View.VISIBLE );
		
		notificationManager.notify(100, notification);
	}

	public int onStartCommand(Intent intent, int flags, int startId) {
		Log.d(TAG, "onStartCommand");
		try
		{
			if( mBgThread != null )
			{
				// prevent running multiple instances
				try
				{
					mBgThread.process.destroy();
					synchronized(mBgThread)
					{
						mBgThread.wait(5000);
					}
					mBgThread = null;
				}
				catch(Exception e)
				{
					e.printStackTrace();
				}
			}
			filesDir = getFilesDir().toString();
			localPath = mPref.getString( "steam_path", "/sdcard/steam/" );
			if( !localPath.endsWith("/") ) localPath += '/';
			notificationInit();
			mBgThread = new BackgroundThread();
			mBgThread.start();
		}
		catch(Exception e)
		{
			printText(e.toString());
		}	
		return super.onStartCommand(intent, flags, startId);
	}

	public void onDestroy() {
		silentKillAll();
		try
		{
			if( mBgThread != null )
			mBgThread.interrupt();
			mBgThread.process.destroy();
		}catch( Exception e ){
			e.printStackTrace();
		}
		mSingleton = null;
		super.onDestroy();
		Log.d(TAG, "onDestroy");
	}

	public IBinder onBind(Intent intent) {
		Log.d(TAG, "onBind");
		return null;
	}
	private void printText(String text)
	{
		// register notification first
		if( notification == null )
			notificationInit();
		long notify = System.currentTimeMillis();
		if( notify - lastNotify >= 100 )
		{
		lastNotify = notify;
        notification.contentView.setTextViewText(R.id.status_text,
												 text);

		notificationManager.notify(100, notification);
		}
		// if activiy exist, print to it's screen
		if( SteamActivity.mSingleton == null )
			return;
		try{
			SteamActivity.mSingleton.printText( text );
		}
		catch( Exception e )
		{
			
		}
	}

	// block current thread and show dialog
	private String promptDialog(String title, String message, boolean passwd)
	{
		Intent intent = new Intent(this, SteamActivity.class);
        final PendingIntent pendingIntent = PendingIntent.getActivity(
			getApplicationContext(), 0, intent, 0);
		// request user interaction
        Notification n = new Notification(R.drawable.ic_launcher, message, System.currentTimeMillis());
        n.flags |= Notification.FLAG_HIGH_PRIORITY;
		//n.priority = Notification.PRIORITY_MAX;
        n.contentIntent = pendingIntent;
		n.contentView = new RemoteViews(getPackageName(), R.layout.notify);
		n.contentView.setViewVisibility(R.id.status_progress, View.GONE);
		n.contentView.setTextViewText( R.id.status_text, message );
		if( notificationManager == null )
			notificationManager = (NotificationManager) getApplicationContext()
				.getSystemService(Context.NOTIFICATION_SERVICE);
		notificationManager.notify(101, n);
		notification.contentView.setTextViewText(R.id.status_text, message);

		notificationManager.notify(100, notification);
		waitForActivity();
		notificationManager.cancel(101);
		if( SteamActivity.mSingleton == null )
			return null;
		try{
			return SteamActivity.mSingleton.promptDialog(title, message, passwd);
		}
		catch( Exception e )
		{
			return null;

		}
	}

	// return next AppID, requested to verify
	private synchronized String getVerify()
	{
		synchronized(mPref)
		{
			Set<String> prefs = mPref.getStringSet("pending_verify",null);
			if( prefs == null || prefs.isEmpty() )
				return null;
			try
			{
				return (String)prefs.toArray()[0];
			}
			catch(Exception e)
			{
				return null;
			}
		}
	}

	// move from pending verification to verified
	private synchronized void setVerified(String id)
	{
		synchronized(mPref)
		{
			Set<String> prefs = mPref.getStringSet("pending_verify",(Set<String>)new HashSet<String>());
			try
			{
				if( !prefs.isEmpty() && prefs.contains(id) )
					prefs.remove(id);
			}
			catch(Exception e){
				prefs = new HashSet<String>();
			}
			Set<String> verified = mPref.getStringSet("verified",new HashSet<String>());
			verified.add( id );
			mPref.edit()
				.putStringSet("verified",verified)
				.putStringSet("pending_verify", prefs)
				.commit();
		}
	}

	// just remove from pending set
	private synchronized void verifyFail(String id)
	{
		synchronized(mPref)
		{
			Set<String> prefs = mPref.getStringSet("pending_verify",(Set<String>)new HashSet<String>());
			try
			{
				if( !prefs.isEmpty() && prefs.contains(id) )
					prefs.remove(id);
			}
			catch(Exception e){
				prefs = new HashSet<String>();
			}
			mPref.edit()
				.putStringSet("pending_verify", prefs)
				.commit();
		}
	}

	// return next AppID, scheduled to download
	private synchronized String getDownload()
	{
		synchronized(mPref)
		{
			Set<String> prefs = mPref.getStringSet("pending_download",null);
			if( prefs == null || prefs.isEmpty() )
				return null;
			try
			{
				return (String)prefs.toArray()[0];
			}
			catch(Exception e)
			{
				return null;
			}
		}
	}

	// remove from download set
	private synchronized void clearDownload(String id)
	{
		synchronized(mPref)
		{
			Set<String> prefs = mPref.getStringSet("pending_download",(Set<String>)new HashSet<String>());
			try
			{
				if( !prefs.isEmpty() && prefs.contains(id) )
					prefs.remove(id);
			}
			catch(Exception e){
				prefs = new HashSet<String>();
			}
			mPref.edit()
				.putStringSet("pending_download", prefs)
				.commit();
		}
	}
	
	private void waitForActivity()
	{
		if( SteamActivity.mSingleton != null )
			// Nothing to wait
			return;
		try
		{
			synchronized( SteamActivity.waitSwitch )
			{
				SteamActivity.waitSwitch.wait();
			}
		}
		catch( Exception e )
		{
			Log.d( TAG, "waitForActivity failed: " + e.toString() );
		}
	}

	// separate thread to control remote process
	class BackgroundThread extends Thread {
		OutputStream processInput = null;

		ProcessState state;;
		private boolean need_reset_config = false;
		private boolean skipQemu = false;
		private String lastID;
		public Process process;
		public AtomicBoolean needDestroy = new AtomicBoolean();
		public InputStream httpInput = null;

		private void downloadFile( String strurl, String path ) throws IOException, CancelException
		{
			URL url = new URL(strurl);
			int count;
			printText("Downloading " + path);
			notification.contentView.setViewVisibility( R.id.status_progress, View.GONE );
			URLConnection conection = url.openConnection();
			conection.connect();

			// this will be useful so that you can show a tipical 0-100%
			// progress bar
			int lenghtOfFile = conection.getContentLength();

			// download the file
			httpInput = new BufferedInputStream(url.openStream(), 8192);

			// Output stream
			OutputStream output = new FileOutputStream(path);

			byte data[] = new byte[1024];

			long total = 0;
			int lastprogress = 0;

			while ( !needDestroy.get() && (count = httpInput.read(data)) != -1) {
				total += count;
				// publishing the progress....
				try
				{
					if( (lenghtOfFile > 0) && ((int) ((total * 100) / lenghtOfFile) - lastprogress > 1) )
						progressUpdate("Downloading " + path, lastprogress = (int) ((total * 100) / lenghtOfFile));
				}
				catch( Exception e ) {}

				// writing data to file
				output.write(data, 0, count);
			}

			// flushing output
			output.flush();

			// closing streams
			output.close();
			httpInput.close();
			httpInput = null;
			if(needDestroy.get())
				throw new CancelException();
		}

		// called on every line, encef with \n
		private void processLine( String str ) throws RestartException,IOException
		{
				// downloading game
				if( str.startsWith( " Update state (") )
				{
					try
					{
					String statestr = str.substring(20).trim();
					//printText(statestr);
					
					String p = statestr.substring(statestr.indexOf('(')+1, statestr.indexOf(')'));
					progressUpdate( statestr, (int)(100 * Float.valueOf(p.substring(0,p.indexOf('/')).trim())/Float.valueOf(p.substring(p.indexOf('/')+1).trim())));
					return;
					}
					catch(Exception e)
					{
						//e.printStackTrace();
					}
				}
				// downloading steam update
				else if( !str.isEmpty() && (str.charAt(0) == '[') && str.contains( "] Downloading update ("))
				{
					try
					{
						progressUpdate(str, (int) (1*Float.valueOf( str.substring(1, str.indexOf('%')).trim())) );
						return;
					}
					catch( Exception e )
					{
						e.printStackTrace();
					}
				}
				// switch to command mode
				else if( str.contains( "Logged in OK" ) )
				{
					state = ProcessState.WAIT;
					printText("Successfully logged in, now starting to send commands");

				}
				// avoid steamcmd bug with permanent login failure
				else if( str.contains( "FAILED with result code " ) )
				{
					// login failure, remove config and try again;
					need_reset_config = true;
					processInput.write("quit\n".getBytes());
					processInput.flush();
					// hack: process does not restart itself, try force restart it;
					try
					{
						sleep(2000);
					}
					catch(Exception e){}
					throw new RestartException();
				}
				// download completed
				else if( str.contains("Success! App '") && ( str.contains("' fully installed." ) || str.contains("' already up to date." ) ) )
				{
					String id = str.substring(str.indexOf("'")+1);
					id = id.substring(0, id.indexOf("'"));
					printText("AppID " + id + " downloaded successfully!");
					clearDownload( id );
				}
				// process hangs up
				else if( str.contains("Fatal assert failed"))
					throw new RestartException();
				// ..AppID %d:\n
				// - release state: ...
				else if( str.contains("AppID ") )
				{
					lastID = str.substring( str.indexOf("AppID ")+ 6, str.indexOf(':'));
				}
				// license status
				else if( str.startsWith(" - release state: ") )
				{
					if( str.contains("Subscribed" ) )
					{
						setVerified( lastID );
						printText("AppID " + lastID + " confirmed");

					}
					else if( str.contains("No License" ) )
					{
						verifyFail( lastID );
						printText("AppID " + lastID + " HAS NO LICENSE");
						// do not try to download it
						clearDownload( lastID );

					}
				}
			notification.contentView.setViewVisibility( R.id.status_progress, View.GONE );
			printText(str);
		}

		// called on every char in line until return true
		private boolean processPartial( String str ) throws CancelException, IOException
		{
			//printText(str);
			{
				if( str.contains( "Steam>" ) )
				{
					// not logged in yet
					if( state == ProcessState.LAUNCH )
					{
						String login = promptDialog("Login", "Please enter your login", false);
						if( login == null )
						{
							processInput.write( ( "exit\n").getBytes() );
							throw new  CancelException();
						}
						else
							processInput.write( ( "login " + login + "\n").getBytes() );
					}
					// already logged in, send commands
					else
					{
						state = ProcessState.COMMAND;
						String cmd = null;
						if( getVerify() != null )
							cmd = "app_status " + getVerify();
						else if( getDownload() != null )
							cmd = "app_update " +  getDownload() + " verify";
						else
							cmd = "exit";
						if( cmd != null )
						{
							processInput.write( ( cmd + '\n').getBytes());
							processInput.flush();
							printText("cmd: " + cmd);
							state = ProcessState.WAIT;
						}
					}
					return true;
				}
				// user interaction
				if( str.startsWith("password:" ))
				{
					String passwd = promptDialog("Password", "Please enter your password", true);
					if( passwd == null )
					{
						processInput.write( ( "\n").getBytes() );
						throw new  CancelException();
					}
					else
						processInput.write( (passwd + '\n').getBytes() );
					return true;
				}
				if( str.startsWith("Steam Guard code:" )|| str.startsWith("Two-factor code:") )
				{
					String passwd = promptDialog("Steam Guard code", "Please enter your SteamGuard code", true);
					if( passwd == null )
					{
						processInput.write( ( "\n").getBytes() );
						throw new  CancelException();
					}
					else
						processInput.write( (passwd + '\n').getBytes() );
					return true;
				}
			}
			return false;
		}

		// launch procesc and process all outout
		private int launchProcess( String command ) throws Exception
		{
			int result = 255;
			printText("process start: " + command);
			process = Runtime.getRuntime().exec( command );
			InputStream reader = process.getInputStream();
			processInput = process.getOutputStream();
			BufferedReader readererr = new BufferedReader(new InputStreamReader(process.getErrorStream()));
			int ch;
			boolean processed = false;

			String str = "";
			while ( !needDestroy.get() && ((ch = reader.read()) >= 0) ) {
				if( ch == '\n' )
				{
					processLine( str );
					str = "";
					processed = false;
				}
				else
					str += (char)ch;
				// performance: skip comparsions on Update state lines
				if( str == " " )
					processed = true;
				if( !processed )
					processed = processPartial( str );
			}
			if(needDestroy.get())
				throw new CancelException();
			processLine( str );
			printText("process closed stdout");

			// flush err buffer
			while( (str = readererr.readLine()) != null && !str.isEmpty() )
				printText(str);
			reader.close();
			processInput.close();

			// Waits for the command to finish.
			if( process != null )
				result = process.waitFor();
			printText("process end: " + result);
			return result;
		}

		final private String repoUrl = "https://raw.githubusercontent.com/mittorn/steamcmd-deps/master/";
		// download from github repo
		private void downloadDep( String path ) throws IOException,CancelException
		{
			downloadFile( repoUrl + path, localPath + path );
		}

		// make directories for local path
		private void mkdirs( String path )
		{
			new File( localPath + path ).mkdirs();
		}

		// download all files if not yet downloaded
		private void downloadAll() throws IOException,CancelException
		{
			if( !skipQemu && !new File( filesDir + "/qemu.downloaded").exists() )
			{
				File qemu = new File( filesDir + "/qemu");
				if( qemu.exists() ) qemu.delete();
				downloadFile( repoUrl + "qemu-armeabi-v7a", filesDir + "/qemu" );
				new File( filesDir + "/qemu.downloaded").createNewFile();
			}
			if( new File( localPath + ".downloaded").exists() )
				return;
			mkdirs( "" );
			downloadDep("gzip");
			mkdirs( "lib" );
			downloadDep( "lib/libc.so.6" );
			downloadDep( "lib/libdl.so.2" );
			downloadDep( "lib/libgcc_s.so.1" );
			downloadDep( "lib/libm.so.6" );
			downloadDep( "lib/libnss_dns.so.2" );
			downloadDep( "lib/libpthread.so.0" );
			downloadDep( "lib/libresolv.so.2" );
			downloadDep( "lib/librt.so.1" );
			mkdirs( "linux32" );
			downloadDep( "linux32/ld-linux.so.2" );
			downloadDep( "preload.so" );
			mkdirs( "sources" );
			downloadDep( "sources/debian.txt" );
			downloadDep( "sources/qemu.patch" );
			downloadDep( "sources/qemu.txt" );
			downloadDep( "sources/preload.c" );
			downloadDep( "tar" );
			downloadDep( "killall" );
			if( skipQemu )
				downloadDep( "start-x86.sh" );
			else
				downloadDep( "start-qemu.sh" );
			downloadFile( "http://media.steampowered.com/client/installer/steamcmd_linux.tar.gz", localPath + "steamcmd_linux.tar.gz" );

			new File( localPath + ".downloaded").createNewFile();
		}
		private int launchX86(String command) throws Exception
		{
			if( skipQemu )
				return launchProcess( "sh " + localPath + "start-x86.sh " + localPath + ' ' + command );
			else
				return launchProcess( "sh " + localPath + "start-qemu.sh " + localPath + " "+ filesDir + "/qemu " + command );
		}
		private void unpackAll() throws Exception
		{
			launchProcess( "chmod 777 " + filesDir + "/qemu" );
			if( new File( localPath + ".unpacked").exists() )
				return;
			launchX86( localPath + "gzip -d steamcmd_linux.tar.gz" );
			launchX86( localPath + "tar xvf steamcmd_linux.tar" );
			new File( localPath + "steamcmd_linux.tar" ).delete();
			new File( localPath + ".unpacked").createNewFile();
		}

		@Override
		public void run() {
			super.run();
			needDestroy.getAndSet(false);
			try {
				if( skipQemu = isX86() )
					localPath = filesDir + '/';
				state = ProcessState.UNPACK;
				downloadAll();
				unpackAll();
				int result;
				do{
					state = ProcessState.LAUNCH;
					killAll();
					if( need_reset_config )
						try{
							new File( localPath + "Steam/config/config.vdf" ).delete();
						}
						catch( Exception e) {}
					try
					{
						result = launchX86( localPath + "linux32/steamcmd" );
					}
					catch( RestartException e )
					{
						// 42 is restart magick in steam
						result = 42;
					}
					needDestroy.getAndSet(false);
				}
				while( result == 42 || getVerify() != null || getDownload() != null ) ;

			} catch (Exception e) {
				printText("Fatal: " + e.toString() + ": "+ e.getMessage());
				e.printStackTrace();
				needDestroy.getAndSet(false);
				killAll();
			}
			finally{}
			printText("Background thread end!");
			mBgThread = null;
			needDestroy.getAndSet(false);
			stopSelf();
			//return null;
		}
		public void killAll()
		{

			// kill old processes, but only if they were running more
			// than 2 seconds to keep killall itself
			try
			{
				if( skipQemu )
					launchX86( localPath + "killall -o5s -9 ld-linux.so.2" );
				else
					launchX86( localPath + "killall -o5s -9 qemu" );
			}
			catch( Exception e ){}
		}
	}
	public void cancelThread()
	{
		if( mBgThread == null )
			return;
		try
		{
			silentKillAll();
			mBgThread.needDestroy.getAndSet(true);
			mBgThread.interrupt();
			// destroy process
			if( mBgThread.process != null )
				mBgThread.process.destroy();
			// cancel stalled download
			if( mBgThread.httpInput != null )
				mBgThread.httpInput.close();
		}catch( Exception e ){
			e.printStackTrace();
		}
	}
	private boolean isX86()
	{
		String s = System.getProperty("ro.product.cpu.abi");

		if( s != null && s.contains("x86"))
			return true;

		s = System.getProperty("ro.product.cpu.abi2");

		if( s != null && s.contains("x86"))
			return true;
		s = System.getProperty("ro.product.cpu.abilist");

		if( s != null && s.contains("x86"))
			return true;

		s = System.getProperty("ro.product.cpu.abilist32");

		if( s != null && s.contains("x86"))
			return true;

		s = System.getProperty("ro.dalvik.vm.isa.arm");

		if( s != null && s.contains("x86"))
			return true;

		return false;
	}
	public void silentKillAll()
	{
		try
		{
			if( isX86() )
				Runtime.getRuntime().exec( "sh " + localPath + "start-x86.sh " + localPath + ' ' + 
					localPath + "killall -o5s -9 ld-linux.so.2" );
			else
				Runtime.getRuntime().exec( "sh " + localPath + "start-qemu.sh " + localPath + " " +
					filesDir + "/qemu " + localPath + "killall -o5s -9 qemu" );
		}
		catch( Exception e ){
			e.printStackTrace();
		}
		
	}
	
}
