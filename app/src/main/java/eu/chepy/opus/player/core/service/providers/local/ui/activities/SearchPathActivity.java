/*
 * SearchPathActivity.java
 *
 * Copyright (c) 2014, Philippe Chepy
 * All rights reserved.
 *
 * This software is the confidential and proprietary information
 * of Philippe Chepy.
 * You shall not disclose such Confidential Information.
 *
 * http://www.chepy.eu
 */
package eu.chepy.opus.player.core.service.providers.local.ui.activities;

import android.app.AlertDialog;
import android.content.ContentValues;
import android.content.Intent;
import android.database.sqlite.SQLiteConstraintException;
import android.database.sqlite.SQLiteDatabase;
import android.os.Bundle;
import android.support.v4.view.MenuItemCompat;
import android.support.v4.view.ViewPager;
import android.support.v7.app.ActionBarActivity;
import android.view.Menu;
import android.view.MenuItem;

import com.astuetz.PagerSlidingTabStrip;

import eu.chepy.opus.player.R;
import eu.chepy.opus.player.core.service.providers.AbstractMediaProvider;
import eu.chepy.opus.player.core.service.providers.local.database.Entities;
import eu.chepy.opus.player.core.service.providers.local.database.OpenHelper;
import eu.chepy.opus.player.core.service.providers.local.ui.fragments.SearchPathFragment;
import eu.chepy.opus.player.ui.activities.UtilDirectorySelectActivity;
import eu.chepy.opus.player.ui.adapter.ux.PagerAdapter;

public class SearchPathActivity extends ActionBarActivity {

	private ViewPager viewPager;
	
	private final static int REQUEST_CODE_SEARCH_PATH = 1;
	
	private final static int REQUEST_CODE_EXCLUDE_PATH = 2;

    private PagerAdapter pagerAdapter;

    private int providerId;
	
	@Override
	protected void onCreate(Bundle bundle) {
		super.onCreate(bundle);

        providerId = getIntent().getIntExtra(AbstractMediaProvider.KEY_PROVIDER_ID, -1);
		
		getSupportActionBar().show();
		
		setContentView(R.layout.activity_library_path);
		
		Bundle searchBundle = new Bundle();
		searchBundle.putInt(SearchPathFragment.CONTENT_TYPE_KEY, SearchPathFragment.CONTENT_SEARCH_PATH);
        searchBundle.putInt(AbstractMediaProvider.KEY_PROVIDER_ID, providerId);
		
		Bundle excludedBundle = new Bundle();
		excludedBundle.putInt(SearchPathFragment.CONTENT_TYPE_KEY, SearchPathFragment.CONTENT_EXCLUDE_PATH);
        excludedBundle.putInt(AbstractMediaProvider.KEY_PROVIDER_ID, providerId);
		
		pagerAdapter = new PagerAdapter(this, getSupportFragmentManager());
		pagerAdapter.addFragment(new SearchPathFragment(), searchBundle, R.string.tab_label_accept_path);
		pagerAdapter.addFragment(new SearchPathFragment(), excludedBundle, R.string.tab_label_exclude_path);
		
        viewPager = (ViewPager)findViewById(R.id.pager_viewpager);
        viewPager.setPageMargin(getResources().getInteger(R.integer.viewpager_margin_width));
        viewPager.setOffscreenPageLimit(pagerAdapter.getCount());
        viewPager.setAdapter(pagerAdapter);
        
        
        PagerSlidingTabStrip scrollingTabs = (PagerSlidingTabStrip) findViewById(R.id.pager_tabs);
		scrollingTabs.setViewPager(viewPager);
		scrollingTabs.setIndicatorColorResource(R.color.view_scrollingtabs_color);
	}

    @Override
	public boolean onCreateOptionsMenu(Menu menu) {
		MenuItem addMenuItem = menu.add(Menu.NONE, 0, 0, R.string.menuitem_label_add_directory);
		addMenuItem.setIcon(R.drawable.ic_action_add_dark);
        MenuItemCompat.setShowAsAction(addMenuItem, MenuItemCompat.SHOW_AS_ACTION_ALWAYS | MenuItemCompat.SHOW_AS_ACTION_WITH_TEXT);
		addMenuItem.setOnMenuItemClickListener(onAddMenuItemListener);

		return true;
	}
	
	@Override
	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (providerId < 0) {
            return;
        }

		switch(requestCode) {
		case REQUEST_CODE_SEARCH_PATH:
			if (resultCode == RESULT_OK) {
                OpenHelper openHelper = new OpenHelper(this, providerId);

                SQLiteDatabase database = openHelper.getWritableDatabase();
                if (database != null) {
                    final ContentValues values = new ContentValues();
                    values.put(Entities.ScanDirectory.COLUMN_FIELD_SCAN_DIRECTORY_IS_EXCLUDED, 0);
                    values.put(Entities.ScanDirectory.COLUMN_FIELD_SCAN_DIRECTORY_NAME, data.getStringExtra(UtilDirectorySelectActivity.KEY_RESULT));

                    try {
                        database.insertOrThrow(Entities.ScanDirectory.TABLE_NAME, null, values);
                    }
                    catch (final SQLiteConstraintException exception) {
                        new AlertDialog.Builder(this)
                                .setTitle(R.string.alert_dialog_title_search_path_exists)
                                .setMessage(R.string.alert_dialog_message_search_path_exists)
                                .setPositiveButton(android.R.string.ok, null)
                                .show();
                    }
                }
			}
			break;
		case REQUEST_CODE_EXCLUDE_PATH:
			if (resultCode == RESULT_OK) {
                OpenHelper openHelper = new OpenHelper(this, providerId);

                SQLiteDatabase database = openHelper.getWritableDatabase();
                if (database != null) {
                    final ContentValues values = new ContentValues();
                    values.put(Entities.ScanDirectory.COLUMN_FIELD_SCAN_DIRECTORY_IS_EXCLUDED, 1);
                    values.put(Entities.ScanDirectory.COLUMN_FIELD_SCAN_DIRECTORY_NAME, data.getStringExtra(UtilDirectorySelectActivity.KEY_RESULT));

                    try {
                        database.insertOrThrow(Entities.ScanDirectory.TABLE_NAME, null, values);
                    }
                    catch (final SQLiteConstraintException exception) {
                        new AlertDialog.Builder(this)
                                .setTitle(R.string.alert_dialog_title_search_path_exists)
                                .setMessage(R.string.alert_dialog_message_search_path_exists)
                                .setPositiveButton(android.R.string.ok, null)
                                .show();
                    }
                }
			}
			break;
		}
        pagerAdapter.doRefresh();
	}
	
	private final MenuItem.OnMenuItemClickListener onAddMenuItemListener = new MenuItem.OnMenuItemClickListener() {

		@Override
		public boolean onMenuItemClick(MenuItem item) {
			Intent intent = new Intent(SearchPathActivity.this, UtilDirectorySelectActivity.class);
			startActivityForResult(intent, viewPager.getCurrentItem() == 0 ? REQUEST_CODE_SEARCH_PATH : REQUEST_CODE_EXCLUDE_PATH);
			return false;
		}
	};
}