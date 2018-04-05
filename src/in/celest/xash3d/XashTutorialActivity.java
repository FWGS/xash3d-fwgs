package in.celest.xash3d;

import android.animation.*;
import android.app.*;
import android.content.*;
import android.os.*;
import android.util.*;
import android.view.*;
import android.view.View.*;
import android.widget.*;
import android.widget.TableRow.*;
import in.celest.xash3d.hl.*;
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
	static class PagedView extends HorizontalScrollView
	{
		boolean isDelayed, isInc, anim;
		int lastScroll, pageWidth, currentPage, numPages, targetPage;
		float firstx,lastx;
		LinearLayout pageContainer;
		ViewGroup.LayoutParams pageParams;

		// allow detect animation end
		static abstract class OnPageListener
		{
			abstract public void onPage(int page);
		}
		OnPageListener listener;

		public PagedView(Context ctx, int pagewidth)
		{
			super(ctx);
			pageContainer = new LinearLayout(ctx);
			pageContainer.setOrientation(LinearLayout.HORIZONTAL);
			setLayoutParams(new ViewGroup.LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.FILL_PARENT));
			setScrollBarStyle(SCROLLBARS_INSIDE_INSET);
			addView(pageContainer);
			pageWidth = pagewidth;
			// this will be applied to every page
			pageParams = new ViewGroup.LayoutParams(pagewidth, LayoutParams.FILL_PARENT);
		}

		private void animateScroll()
		{
			if( !anim )
			{
				// allow only correct position if anim disabled
				scrollTo(pageWidth*currentPage,0);
				return;
			}
			if( isInc && lastScroll >= pageWidth * targetPage || !isInc && lastScroll <= pageWidth * targetPage )
			{
				// got target page, stop now
				anim = false;
				currentPage = targetPage;
				scrollTo(pageWidth*targetPage,0);
				if( listener != null )
					listener.onPage(currentPage);
				return;
			}

			if( !isDelayed ) //semaphore
			{
				isDelayed = true;
				postDelayed(new Runnable()
					{
						public void run()
						{
							isDelayed = false;
							// animate to 1/50 of page every 10 ms
							scrollBy(isInc?pageWidth/50:-pageWidth/50,0);
						}
					},10);
			}
		}

		// add view and set layout
		public void addPage(View view)
		{
			view.setLayoutParams(pageParams);
			pageContainer.addView(view);
			numPages++;
		}

		@Override
		protected void onScrollChanged(int l, int t, int oldl, int oldt) 
		{
			// this called on every scrollTo/scrollBy and touch scroll
			super.onScrollChanged(l,t,oldl,oldt);
			lastScroll=l;
			isInc = l>oldl;
			animateScroll();
		}

		@Override
		public boolean onTouchEvent(MotionEvent e)
		{
			switch( e.getAction() )
			{
				case MotionEvent.ACTION_DOWN:
					// store swipe start
					lastx = firstx = e.getX();
					// animation will be started on next scroll event
					anim = true;
					break;
				case MotionEvent.ACTION_MOVE:
					// animation will start in supercall, so select direction now
					isInc = e.getX() < lastx;
					targetPage = isInc?currentPage+1:currentPage-1;
					lastx = e.getX();
					break;
				case MotionEvent.ACTION_UP:
					// detect misstouch (<100 pixels)
					if( Math.abs(e.getX()-firstx) < 100)
					{
						/*
						anim = false;
						targetPage = currentPage;
						scrollTo(currentPage*pageWidth,0);*/
						targetPage = currentPage;
						isInc = currentPage * pageWidth > lastScroll;
					}
					return false;
			}


			return super.onTouchEvent(e);
		}

		// set page number
		public void changePage(int page)
		{
			targetPage = page;
			anim = true;
			isInc = targetPage > currentPage;
			animateScroll();
		}

		// call when animation ends
		public void setOnPageListener(OnPageListener listener1)
		{
			listener = listener1;
		}
	}

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
    }

    private void controlPosition()
	{
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
            super.onBackPressed();
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
