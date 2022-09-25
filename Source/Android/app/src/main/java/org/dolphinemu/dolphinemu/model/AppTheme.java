package org.dolphinemu.dolphinemu.model;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;

import androidx.annotation.StyleRes;

import org.dolphinemu.dolphinemu.NativeLibrary;
import org.dolphinemu.dolphinemu.R;
import org.dolphinemu.dolphinemu.DolphinApplication;
import org.dolphinemu.dolphinemu.features.settings.model.Settings;
import org.dolphinemu.dolphinemu.features.settings.model.AbstractStringSetting;
import org.dolphinemu.dolphinemu.ui.main.MainActivity;
import org.dolphinemu.dolphinemu.ui.main.TvMainActivity;
import org.dolphinemu.dolphinemu.activities.EmulationActivity;
import org.dolphinemu.dolphinemu.features.settings.ui.SettingsActivity;

public class AppTheme
{
  public static final String APP_THEME = "appTheme";

  public static final String MMJR_AMOLED = "mmjramoled";
  public static final String DOLPHIN_BLUE = "dolphinblue";
  public static final String LUIGI_GREEN = "luigigreen";

  public static final String DEFAULT = APP_THEME;

  public static AbstractStringSetting APPLICATION_THEME = new AbstractStringSetting() {
    @Override
    public String getString(Settings settings)
    {
      final SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(DolphinApplication.getAppContext());
      return preferences.getString(APP_THEME, DEFAULT);
    }

    @Override
    public void setString(Settings settings, String newValue)
    {
      final SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(DolphinApplication.getAppContext());
      final SharedPreferences.Editor editor = preferences.edit();
      editor.putString(APP_THEME, newValue).apply();
    }

    @Override
    public boolean isOverridden(Settings settings)
    {
      return false;
    }

    @Override
    public boolean isRuntimeEditable()
    {
      return false;
    }

    @Override
    public boolean delete(Settings settings)
    {
      final SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(DolphinApplication.getAppContext());
      final SharedPreferences.Editor editor = preferences.edit();
      editor.remove(APP_THEME).apply();
      return true;
    }
  };

  public static void applyTheme(Activity activity)
  {
    String theme = APPLICATION_THEME.getString(null);
    @StyleRes int themeId;

    if (activity instanceof SettingsActivity)
    {
      switch (theme)
      {

        case AppTheme.DOLPHIN_BLUE:
          themeId = R.style.Theme_DolphinSettings_DolphinBlue;
          break;

        default:
          themeId = R.style.Dolphin_Material_SettingsBase;
          break;
      }
    }
    else if (activity instanceof EmulationActivity)
    {
      switch (theme)
      {

        case AppTheme.DOLPHIN_BLUE:
          themeId = R.style.DolphinBase;
          break;

        default:
          themeId = R.style.DolphinBase;
          break;
      }
    }
    else if (activity instanceof TvMainActivity)
    {
      switch (theme)
      {
        case AppTheme.DOLPHIN_BLUE:
          themeId = R.style.Theme_DolphinTv_DolphinBlue;
          break;

        default:
          themeId = R.style.DolphinBase;
          break;
      }
    }
    else
    {
      switch (theme)
      {

        case AppTheme.DOLPHIN_BLUE:
          themeId = R.style.Theme_DolphinMain_DolphinBlue;
          break;
        default:
          themeId = R.style.DolphinBase;
          break;
      }
    }

    activity.setTheme(themeId);
  }

  public static SharedPreferences.OnSharedPreferenceChangeListener onThemeSettingChanged = (sharedPreferences, key) ->
  {
    if (key.equals(AppTheme.APP_THEME))
    {
      final String newValue = sharedPreferences.getString(APP_THEME, DEFAULT);
      NativeLibrary.setNativeTheme(newValue);
      onThemeChanged();
    }
  };

  public static void onThemeChanged()
  {
    Context context = DolphinApplication.getAppContext();
    Intent intent = new Intent(context, MainActivity.class);
    intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
    context.startActivity(intent);
  }
}
