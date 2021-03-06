/*
 * LibraryAdapter.java
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
package net.opusapp.player.ui.adapter;

import android.app.Activity;
import android.database.Cursor;
import android.support.v4.widget.SimpleCursorAdapter;
import android.support.v7.widget.PopupMenu;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;

import com.bumptech.glide.Glide;

import net.opusapp.player.R;
import net.opusapp.player.ui.utils.PlayerApplication;

public class LibraryAdapter extends SimpleCursorAdapter {

    public static final String TAG = LibraryAdapter.class.getSimpleName();


    protected LibraryAdapterContainer container;



    protected int itemView;

    protected int managerType;


    /*
        Text resources
     */
    protected int[] textColumns;

    protected int[] textViews;



    /*
        Image resources
     */
    protected int idColumn;

    protected int artUriColumn;

    protected int imagePlaceHolder;

    protected int imageView;

    protected int visibilityColumn;

    protected boolean transparentBg;

    protected long positionIndicator;

    protected int positionIndicatorView;



    protected int source;



    public static final int PLAYER_MANAGER = 0;

    public static final int LIBRARY_MANAGER = 1;



    public LibraryAdapter(LibraryAdapterContainer adapterContainer, int source, int managerType, int itemView, int[] textColumns, int[] textViews, int idColumn, int artUriColumn, int imagePlaceHolder, int imageView, int visibilityColumn) {
        this(adapterContainer, source, managerType, itemView, textColumns, textViews, idColumn, artUriColumn, imagePlaceHolder, imageView, visibilityColumn, -1);
    }

    public LibraryAdapter(LibraryAdapterContainer container, int source, int managerType, int itemView, int[] textColumns, int[] textViews, int idColumn, int artUriColumn, int imagePlaceHolder, int imageView, int visibilityColumn, int indicator) {
        super(PlayerApplication.context, itemView, null, new String[] {}, new int[] {}, 0);

        this.container = container;

        this.source = source;
        this.managerType = managerType;

        this.itemView = itemView;

        this.textColumns = textColumns.clone();
        this.idColumn = idColumn;
        this.artUriColumn = artUriColumn;
        this.imagePlaceHolder = imagePlaceHolder;

        this.textViews = textViews.clone();
        this.imageView = imageView;

        this.visibilityColumn = visibilityColumn;

        this.transparentBg = false;
        positionIndicator = -1;
        positionIndicatorView = indicator;
    }

    public void setTransparentBackground(boolean transparentBackground) {
        transparentBg = transparentBackground;
    }

    @Override
    public View getView(final int position, View convertView, final ViewGroup parent) {
        //final View view = super.getView(position, convertView, parent);
        final Cursor cursor = (Cursor) getItem(position);

        LibraryHolder viewHolder;

        if (cursor == null) {
            return convertView;
        }


        if (convertView == null) {
            final LayoutInflater layoutInflater = LayoutInflater.from (PlayerApplication.context);
            convertView = layoutInflater.inflate(itemView, parent, false);

            viewHolder = new LibraryHolder();
            viewHolder.textViews = new TextView[textViews.length];
            for (int textIndex = 0; textIndex < textColumns.length; textIndex++) {
                viewHolder.textViews[textIndex] = (TextView) convertView.findViewById(textViews[textIndex]);
            }

            viewHolder.imageView = (ImageView) convertView.findViewById(imageView);

            if (positionIndicatorView >= 0) {
                viewHolder.positionIndicatorView = (ImageView) convertView.findViewById(positionIndicatorView);
            }
            else {
                viewHolder.positionIndicatorView = null;
            }

            viewHolder.contextMenuHandle = convertView.findViewById(R.id.context_menu_handle);

            convertView.setTag(viewHolder);
        } else {
            viewHolder = (LibraryHolder) convertView.getTag();
        }

        if (viewHolder != null) {
            for (int textIndex = 0; textIndex < textColumns.length; textIndex++) {
                viewHolder.textViews[textIndex].setText(cursor.getString(textColumns[textIndex]));
            }

            if (artUriColumn >= 0) {
                viewHolder.imageView.setImageResource(imagePlaceHolder);

                String imageUri = null;

                switch (source) {
                    case LibraryAdapterFactory.ADAPTER_SONG:
                    case LibraryAdapterFactory.ADAPTER_PLAYLIST_DETAILS:
                    case LibraryAdapterFactory.ADAPTER_ALBUM:
                    case LibraryAdapterFactory.ADAPTER_ALBUM_SIMPLE:
                    case LibraryAdapterFactory.ADAPTER_STORAGE:
                        imageUri = cursor.getString(artUriColumn);
                        break;
                }

                if (imageUri != null) {
                    final String DRAWABLE_URI = "drawable://";
                    if (imageUri.startsWith(DRAWABLE_URI)) {
                        int resourceId = Integer.parseInt(imageUri.substring(DRAWABLE_URI.length()));

                        Glide.with(PlayerApplication.context)
                                .load(resourceId)
                                .centerCrop()
                                .crossFade()
                                .into(viewHolder.imageView);
                    }
                    else {
                        Glide.with(PlayerApplication.context)
                                .load(imageUri)
                                .centerCrop()
                                .placeholder(imagePlaceHolder)
                                .crossFade()
                                .into(viewHolder.imageView);
                    }
                }
                else {
                    Glide.with(PlayerApplication.context)
                            .load(imagePlaceHolder)
                            .centerCrop()
                            .crossFade()
                            .into(viewHolder.imageView);
                }
            }

            if (viewHolder.contextMenuHandle != null) {
                if (container == null) {
                    viewHolder.contextMenuHandle.setVisibility(View.GONE);
                }
                else {
                    viewHolder.contextMenuHandle.setOnClickListener(new View.OnClickListener() {

                        @Override
                        public void onClick(View view) {
                            PopupMenu popupMenu = new PopupMenu(container.getActivity(), view);
                            container.createMenu(popupMenu.getMenu(), position);
                            popupMenu.setOnMenuItemClickListener(container.getOnPopupMenuItemClickListener(position));
                            popupMenu.show();
                        }
                    });
                }
            }

            if (visibilityColumn >= 0) {
                if (cursor.getInt(visibilityColumn) == 0) {
                    convertView.setBackgroundColor(PlayerApplication.context.getResources().getColor(R.color.holo_orange));
                }
                else {
                    if (transparentBg) {
                        convertView.setBackgroundResource(R.drawable.song_list_no_background);
                    }
                    else {
                        convertView.setBackgroundResource(R.drawable.song_list);
                    }
                }
            }

            if (viewHolder.positionIndicatorView != null) {
                if (cursor.getPosition() == positionIndicator) {
                    viewHolder.positionIndicatorView.setVisibility(View.VISIBLE);
                }
                else {
                    viewHolder.positionIndicatorView.setVisibility(View.GONE);
                }
            }
        }

        return convertView;
    }

    public void setPosition(long position) {
        positionIndicator = position;
    }



    private final class LibraryHolder {

        public TextView textViews[];

        public ImageView imageView;

        public ImageView positionIndicatorView;

        public View contextMenuHandle;
    }

    public interface LibraryAdapterContainer {
        public Activity getActivity();

        public PopupMenu.OnMenuItemClickListener getOnPopupMenuItemClickListener(int position);

        public void createMenu(final Menu menu, int position);
    }
}
