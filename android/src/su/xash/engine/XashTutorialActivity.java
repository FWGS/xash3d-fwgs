package su.xash.engine;

import android.animation.*;
import android.app.*;
import android.content.*;
import android.os.*;
import android.util.*;
import android.view.*;
import android.view.View.*;
import android.widget.*;
import android.widget.TableRow.*;
import su.xash.engine.*;
import java.util.*;
import su.xash.fwgslib.*;

import android.view.View.MeasureSpec;

public class XashTutorialActivity extends Activity implements View.OnClickListener
{
	private Button next, prev;
    private LinearLayout indicatorLayout;
    private FrameLayout containerLayout;
    private RelativeLayout buttonContainer;

    private int currentItem;

    private int prevText, nextText, finishText, cancelText, numPages;

	PagedView scroll;
	
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
		if( FWGSLib.sdk >= 21 ) // material
			setTheme(0x0103022e); // Theme_Material_NoActionBar
		else
			setTheme(0x01030006); // Theme_NoTitleBar
		setContentView(R.layout.activity_tutorial);
		initTexts();
		initViews();
		initPages();
		changeFragment(0);
    }
	

    private void initTexts() {
        prevText = R.string.prev;
        cancelText = R.string.skip;
        finishText = R.string.finish;
        nextText = R.string.next;
    }

    private void initPages() {
		LayoutInflater inflater;

		inflater = (LayoutInflater) this.getSystemService(Context.LAYOUT_INFLATER_SERVICE);              
		ViewGroup container = (ViewGroup)findViewById(R.id.container);

		DisplayMetrics metrics = new DisplayMetrics();
		getWindowManager().getDefaultDisplay().getMetrics(metrics);

		scroll = new PagedView(this,metrics.widthPixels);
		container.addView(scroll);

		while( true )
		{
			int titleres = getResources().getIdentifier("page_title" + String.valueOf(numPages), "string", getPackageName());
			
			if( titleres == 0 )
				break;
			
			ViewGroup layout = (ViewGroup) inflater.inflate(R.layout.tutorial_step , null);
			
			TextView title = (TextView) layout.findViewById(R.id.title);
			TextView text = (TextView) layout.findViewById(R.id.content);
			ImageView drawable = (ImageView) layout.findViewById(R.id.image);

			int contentres = getResources().getIdentifier("page_content" + String.valueOf(numPages), "string", getPackageName());
			int drawableres = getResources().getIdentifier("page" + String.valueOf(numPages), "drawable", getPackageName());
			if( drawableres == 0)
				drawableres = getResources().getIdentifier("page" + String.valueOf(numPages) + "_" + Locale.getDefault().getLanguage(), "drawable", getPackageName());
			if( drawableres == 0 )
				drawableres = getResources().getIdentifier("page" + String.valueOf(numPages) + "_en", "drawable", getPackageName());

			title.setText(titleres);
			text.setText(contentres);
			if( FWGSLib.isLandscapeOrientation(this) )
				drawable.setScaleType(ImageView.ScaleType.FIT_CENTER);
			drawable.setImageResource(drawableres);
			scroll.addPage(layout);
			numPages++;
		}
		scroll.setOnPageListener(new PagedView.OnPageListener(){
			@Override
			public void onPage(int page)
			{
				currentItem = page;
				controlPosition();
			}
		});
		if( FWGSLib.sdk < 14 ) // pre-ics does not apply buttons background
			FWGSLib.changeButtonsStyle((ViewGroup)container.getParent());
    }

    private void controlPosition()
	{
        if( currentItem > numPages - 1 )
        	currentItem = numPages - 1;

        notifyIndicator();
        if (currentItem == numPages - 1) {
            next.setText(finishText);
            prev.setText(prevText);
        } else if ( currentItem == 0) {
            prev.setText(cancelText);
            next.setText(nextText);
        } else {
            prev.setText(prevText);
            next.setText(nextText);
        }
	}

    private void initViews() {
        currentItem = 0;

        next = (Button) findViewById(R.id.next);
        prev = (Button) findViewById(R.id.prev);

        indicatorLayout = (LinearLayout) findViewById(R.id.indicatorLayout);
        containerLayout = (FrameLayout) findViewById(R.id.containerLayout);
        buttonContainer = (RelativeLayout) findViewById(R.id.buttonContainer);

        next.setOnClickListener(this);
        prev.setOnClickListener(this);
    }

    public void notifyIndicator() {
        if (indicatorLayout.getChildCount() > 0)
            indicatorLayout.removeAllViews();

        for (int i = 0; i < 5; i++) {
            ImageView imageView = new ImageView(this);
            imageView.setPadding(8, 8, 8, 8);
            int drawable = R.drawable.circle_black;
            if (i == currentItem)
                drawable = R.drawable.circle_white;

            imageView.setImageResource(drawable);

            final int finalI = i;
            imageView.setOnClickListener(new View.OnClickListener() {
					@Override
					public void onClick(View v) {
						changeFragment(finalI);
					}
				});

            indicatorLayout.addView(imageView);
        }

    }

    @Override
    public void onBackPressed() {
       if (currentItem == 0) {
            finish();
        } else {
            changeFragment(false);
        }
    }

    @Override
    public void onClick(View v) {
        if (v.getId() == R.id.next) {
			if( currentItem == numPages - 1 )
			{
				finish();
				return;
			}
            changeFragment(true);
        } else if (v.getId() == R.id.prev) {
			if( currentItem == 0 )
			{
				finish();
				return;
			}
            changeFragment(false);
        }
    }

    private void changeFragment(int position) {
        scroll.changePage(position);
    }

    private void changeFragment(boolean isNext) {
        if (isNext) {
            scroll.changePage(currentItem+1);
        } else {
            scroll.changePage(currentItem-1);
        }
    }

    public void setPrevText(int text) {
        prevText = text;
    }

    public void setNextText(int text) {
        nextText = text;
    }

    public void setFinishText(int text) {
        finishText = text;
    }

    public void setCancelText(int text) {
        cancelText = text;
    }
}
