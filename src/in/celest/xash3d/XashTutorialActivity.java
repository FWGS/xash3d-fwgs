package in.celest.xash3d;

import android.app.*;
import android.os.*;
import android.view.*;
import android.widget.*;
import in.celest.xash3d.hl.*;
import java.util.*;
import su.xash.fwgslib.*;

public class XashTutorialActivity extends Activity implements View.OnClickListener
{
	private Button next, prev;
    private LinearLayout indicatorLayout;
    private FrameLayout containerLayout;
    private RelativeLayout buttonContainer;
	private TextView title;
	private TextView text;
	private ImageView drawable;

    private int currentItem;

    private int prevText, nextText, finishText, cancelText;
	
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
		changeFragment(0);
		
    }
	

    private void initTexts() {
        prevText = R.string.prev;
        cancelText = R.string.cancel;
        finishText = R.string.finish;
        nextText = R.string.next;
    }

    private void initAdapter() {
        /*adapter = new StepPagerAdapter(getSupportFragmentManager(), steps);
        pager.setAdapter(adapter);
        pager.setOnPageChangeListener(new ViewPager.OnPageChangeListener() {
				@Override
				public void onPageScrolled(int position, float positionOffset, int positionOffsetPixels) {

				}

				@Override
				public void onPageSelected(int position) {
					currentItem = position;
					controlPosition(position);
				}

				@Override
				public void onPageScrollStateChanged(int state) {

				}
			});*/
    }

    private void changeStatusBarColor(int backgroundColor) {
        // Window window = getWindow();
        // window.clearFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_STATUS);
        // window.addFlags(0x80000000) //WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS
        // window.setStatusBarColor(backgroundColor);
    }

    private void controlPosition( boolean last) {
        notifyIndicator();
        if (last) {
            next.setText(finishText);
            prev.setText(prevText);
        } else if ( currentItem == 0) {
            prev.setText(cancelText);
            next.setText(nextText);
        } else {
            prev.setText(prevText);
            next.setText(nextText);
        }

        //containerLayout.setBackgroundColor(steps.get(position).getBackgroundColor());
        //buttonContainer.setBackgroundColor(steps.get(position).getBackgroundColor());
    }

    private void initViews() {
        currentItem = 0;

       // pager = (ViewPager) findViewById(R.id.viewPager);
        next = (Button) findViewById(R.id.next);
        prev = (Button) findViewById(R.id.prev);
        indicatorLayout = (LinearLayout) findViewById(R.id.indicatorLayout);
        containerLayout = (FrameLayout) findViewById(R.id.containerLayout);
        buttonContainer = (RelativeLayout) findViewById(R.id.buttonContainer);
		title = (TextView) findViewById(R.id.title);
		text = (TextView) findViewById(R.id.content);
		drawable = (ImageView) findViewById(R.id.image);

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
            super.onBackPressed();
        } else {
            changeFragment(false);
        }
    }

    @Override
    public void onClick(View v) {
        if (v.getId() == R.id.next) {
            changeFragment(true);
        } else if (v.getId() == R.id.prev) {
            changeFragment(false);
        }
    }

    private void changeFragment(int position) {
        currentItem = position;
		updateStep();
    }

    private void changeFragment(boolean isNext) {
        if (isNext) {
            currentItem++;
        } else {
            currentItem--;
        }
            updateStep();
    }


	void updateStep()
	{
		int titleres = getResources().getIdentifier("page_title" + String.valueOf(currentItem), "string", getPackageName());

		if( titleres == 0 )
		{
			finish();
			return;
		}
		int titlenext = getResources().getIdentifier("page_title" + String.valueOf(currentItem+1), "string", getPackageName());
		int contentres = getResources().getIdentifier("page_content" + String.valueOf(currentItem), "string", getPackageName());
		int drawableres = getResources().getIdentifier("page" + String.valueOf(currentItem), "drawable", getPackageName());
		if( drawableres == 0)
			drawableres = getResources().getIdentifier("page" + String.valueOf(currentItem) + "_" + Locale.getDefault().getLanguage(), "drawable", getPackageName());
		if( drawableres == 0 )
			drawableres = getResources().getIdentifier("page" + String.valueOf(currentItem) + "_en", "drawable", getPackageName());
		
		title.setText(titleres);
		text.setText(contentres);
		drawable.setImageResource(drawableres);
		controlPosition(titlenext == 0);
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
