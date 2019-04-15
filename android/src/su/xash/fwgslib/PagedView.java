package su.xash.fwgslib;

import android.view.*;
import android.view.View.*;
import android.widget.*;
import android.content.Context;

public class PagedView extends HorizontalScrollView
{
	boolean isDelayed, isInc, anim;
	int lastScroll, pageWidth, currentPage, numPages, targetPage;
	float firstx,lastx;
	LinearLayout pageContainer;
	ViewGroup.LayoutParams pageParams;

	// allow detect animation end
	public static abstract class OnPageListener
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
