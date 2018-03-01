package in.celest.xash3d;

import com.hololo.tutorial.library.TutorialActivity;
import com.hololo.tutorial.library.Step;
import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.support.v4.view.ViewPager;
import android.support.v4.app.FragmentActivity;
// import android.support.v7.app.AppCompatActivity;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.graphics.Color;

import java.util.ArrayList;
import java.util.List;

import in.celest.xash3d.hl.R;

public class XashTutorialActivity extends TutorialActivity
{
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

		addFragment(new Step.Builder().setTitle(getString(R.string.page_title0))
			.setContent(getString(R.string.page_content0))
			.setBackgroundColor(Color.parseColor("#555555")) // int background color
			.setDrawable(R.drawable.page0) // int top drawable
			.build());

		addFragment(new Step.Builder().setTitle(getString(R.string.page_title1))
			.setContent(getString(R.string.page_content1))
			.setBackgroundColor(Color.parseColor("#555555")) // int background color
			.setDrawable(R.drawable.page1_en) // int top drawable
			.build());
		addFragment(new Step.Builder().setTitle(getString(R.string.page_title2))
			.setContent(getString(R.string.page_content2))
			.setBackgroundColor(Color.parseColor("#555555")) // int background color
			.setDrawable(R.drawable.page2_en) // int top drawable
			.build());
		addFragment(new Step.Builder().setTitle(getString(R.string.page_title3))
			.setContent(getString(R.string.page_content3))
			.setBackgroundColor(Color.parseColor("#555555")) // int background color
			.setDrawable(R.drawable.page3_en) // int top drawable
			.build());
		addFragment(new Step.Builder().setTitle(getString(R.string.page_title4))
			.setContent(getString(R.string.page_content4))
			.setBackgroundColor(Color.parseColor("#555555")) // int background color
			.setDrawable(R.drawable.page4_en)
			.build());

    }
}
