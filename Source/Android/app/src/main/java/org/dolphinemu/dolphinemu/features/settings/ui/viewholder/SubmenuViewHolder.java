// SPDX-License-Identifier: GPL-2.0-or-later

package org.dolphinemu.dolphinemu.features.settings.ui.viewholder;

import android.graphics.drawable.Drawable;
import android.view.View;
import android.widget.TextView;

import androidx.annotation.Nullable;
import androidx.appcompat.content.res.AppCompatResources;

import org.dolphinemu.dolphinemu.R;
import org.dolphinemu.dolphinemu.features.settings.model.view.SettingsItem;
import org.dolphinemu.dolphinemu.features.settings.model.view.SubmenuSetting;
import org.dolphinemu.dolphinemu.features.settings.ui.SettingsAdapter;

import java.util.ArrayList;
import java.util.List;

public final class SubmenuViewHolder extends SettingViewHolder
{
  private SubmenuSetting mItem;

  private TextView mTextSettingName;
  private View view;

  public SubmenuViewHolder(View itemView, SettingsAdapter adapter)
  {
    super(itemView, adapter);
  }

  @Override
  protected void findViews(View root)
  {
    view=root;
    mTextSettingName = root.findViewById(R.id.text_setting_name);
  }

  @Override
  public void bind(SettingsItem item)
  {
    mItem = (SubmenuSetting) item;

    mTextSettingName.setText(item.getName());

    List<String> enhancements_hacks = new ArrayList<>();
    enhancements_hacks.add("custom_textures");
    Drawable drawable;

    if (!enhancements_hacks.contains(((SubmenuSetting) item).getMenuKey().getTag())){
      drawable = AppCompatResources.getDrawable(view.getContext(), view.getResources()
        .getIdentifier("ic_"+item.getName().toString().toLowerCase(), "drawable", view.getContext().getPackageName()));

      drawable.setBounds(0, 0, 60, 60);

      mTextSettingName.setCompoundDrawables(drawable, null, null, null);
    }
    else {
      drawable = AppCompatResources.getDrawable(view.getContext(), view.getResources()
        .getIdentifier("ic_"+(((SubmenuSetting) item).getMenuKey().getTag().toString().toLowerCase()), "drawable", view.getContext().getPackageName()));

      drawable.setBounds(0, 0, 60, 60);

      mTextSettingName.setCompoundDrawables(drawable, null, null, null);
    }
  }

  @Override
  public void onClick(View clicked)
  {
    getAdapter().onSubmenuClick(mItem);
  }

  @Nullable @Override
  protected SettingsItem getItem()
  {
    return mItem;
  }
}
