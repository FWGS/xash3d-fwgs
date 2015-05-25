package in.celest.xash3d;

import android.app.Activity;
import android.os.Bundle;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.content.Intent;
import android.widget.EditText;

import in.celest.xash3d.hl.R;

public class LauncherActivity extends Activity {

    public final static String ARGV = "in.celest.xash3d.MESSAGE";
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_launcher);
    }
    
    public void startXash(View view)
    {
	Intent intent = new Intent(this, org.libsdl.app.SDLActivity.class);
	EditText cmdArgs = (EditText)findViewById(R.id.cmdArgs);
	String sArgv = cmdArgs.getText().toString();
	
	intent.putExtra(ARGV, sArgv);
	
	startActivity(intent);
    }


    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Inflate the menu; this adds items to the action bar if it is present.
        //getMenuInflater().inflate(R.menu.menu_launcher, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        // Handle action bar item clicks here. The action bar will
        // automatically handle clicks on the Home/Up button, so long
        // as you specify a parent activity in AndroidManifest.xml.
        int id = item.getItemId();

        //noinspection SimplifiableIfStatement
        /*if (id == R.id.action_settings) {
            return true;
        }*/

        return super.onOptionsItemSelected(item);
    }
}
