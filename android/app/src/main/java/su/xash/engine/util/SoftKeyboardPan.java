package su.xash.engine.util;

import android.app.Activity;
import android.graphics.Rect;
import android.view.View;
import android.widget.FrameLayout;

public class SoftKeyboardPan {
	// Fullscreen windows are never adjusted for the soft keyboard, whatever windowSoftInputMode
	// says, see legendary bug https://code.google.com/p/android/issues/detail?id=5497
	//
	// The well-known workaround for this bug shrinks the content view, but that makes SDL resize
	// its surface and the engine restart the video mode just to show the keyboard. Shift the view
	// up instead, keeping the surface size intact, like adjustPan would have done.
	//
	// To use this class, simply invoke assistActivity() on an Activity that already has its content view set.

	public static void assistActivity(Activity activity) {
		new SoftKeyboardPan(activity);
	}

	private final View mChildOfContent;

	private SoftKeyboardPan(Activity activity) {
		FrameLayout content = activity.findViewById(android.R.id.content);
		mChildOfContent = content.getChildAt(0);
		mChildOfContent.getViewTreeObserver().addOnGlobalLayoutListener(this::panChildOfContent);
	}

	private void panChildOfContent() {
		int usableHeight = computeUsableHeight();
		int fullHeight = mChildOfContent.getRootView().getHeight();
		int keyboardHeight = fullHeight - usableHeight;
		int offset = 0;

		// keyboard probably just became visible, if it covers more than a quarter of the screen
		if (keyboardHeight > (fullHeight / 4)) {
			offset = Math.max(getFocusBottom(fullHeight) - usableHeight, 0);
			offset = Math.min(offset, keyboardHeight);
		}

		mChildOfContent.setTranslationY(-offset);
	}

	// bottom of the view that receives the text, unpanned. SDL keeps its dummy text edit
	// at the rect the engine passes to SDL_SetTextInputRect
	private int getFocusBottom(int fullHeight) {
		View focus = mChildOfContent.findFocus();

		if (focus == null) {
			return fullHeight;
		}

		int[] focusPos = new int[2];
		int[] contentPos = new int[2];
		focus.getLocationInWindow(focusPos);
		mChildOfContent.getLocationInWindow(contentPos);

		return focusPos[1] - contentPos[1] + focus.getHeight();
	}

	private int computeUsableHeight() {
		Rect r = new Rect();
		mChildOfContent.getWindowVisibleDisplayFrame(r);
		return (r.bottom - r.top);
	}
}
