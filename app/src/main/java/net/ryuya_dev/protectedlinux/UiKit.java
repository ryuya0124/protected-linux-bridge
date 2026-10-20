package net.ryuya_dev.protectedlinux;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.drawable.*;
import android.view.*;
import android.widget.*;
import androidx.appcompat.view.ContextThemeWrapper;
import androidx.core.widget.TextViewCompat;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.card.MaterialCardView;
import com.google.android.material.color.MaterialColors;

/** Shared Material 3 hierarchy: section lists, scoped cards and a single primary action. */
final class UiKit {
  private UiKit() {}

  static int dp(Context context, int value) {
    return Math.round(value * context.getResources().getDisplayMetrics().density);
  }

  static int color(Context context, int attribute) {
    return MaterialColors.getColor(context, attribute, 0xff6750a4);
  }

  static LinearLayout column(Context context) {
    LinearLayout view = new LinearLayout(context);
    view.setOrientation(LinearLayout.VERTICAL);
    return view;
  }

  static TextView text(Context context, CharSequence value, int appearance) {
    TextView view = new TextView(context);
    view.setText(value);
    TextViewCompat.setTextAppearance(view, appearance);
    view.setTextColor(color(context, com.google.android.material.R.attr.colorOnSurface));
    return view;
  }

  static void heading(LinearLayout parent, int title, int subtitle) {
    Context context = parent.getContext();
    TextView heading =
        text(
            context,
            context.getText(title),
            com.google.android.material.R.style.TextAppearance_Material3_HeadlineLarge);
    heading.setPadding(0, dp(context, 20), 0, dp(context, 8));
    parent.addView(heading);
    if (subtitle != 0) {
      TextView detail =
          text(
              context,
              context.getText(subtitle),
              com.google.android.material.R.style.TextAppearance_Material3_BodyMedium);
      detail.setTextColor(color(context, com.google.android.material.R.attr.colorOnSurfaceVariant));
      detail.setPadding(0, 0, 0, dp(context, 20));
      parent.addView(detail);
    }
  }

  static void section(LinearLayout parent, int title) {
    Context context = parent.getContext();
    TextView label =
        text(
            context,
            context.getText(title),
            com.google.android.material.R.style.TextAppearance_Material3_LabelLarge);
    label.setTextColor(color(context, androidx.appcompat.R.attr.colorPrimary));
    label.setPadding(dp(context, 4), dp(context, 24), 0, dp(context, 8));
    parent.addView(label);
  }

  static MaterialCardView surface(Context context) {
    MaterialCardView card = new MaterialCardView(context);
    card.setRadius(dp(context, 24));
    card.setStrokeWidth(0);
    card.setCardElevation(0);
    card.setCardBackgroundColor(
        color(context, com.google.android.material.R.attr.colorSurfaceContainerLow));
    card.setClipToOutline(true);
    return card;
  }

  static MaterialCardView card(LinearLayout parent) {
    Context context = parent.getContext();
    MaterialCardView card = surface(context);
    LinearLayout.LayoutParams layout = new LinearLayout.LayoutParams(-1, -2);
    layout.setMargins(0, dp(context, 6), 0, dp(context, 6));
    parent.addView(card, layout);
    return card;
  }

  static LinearLayout group(LinearLayout parent) {
    LinearLayout rows = column(parent.getContext());
    card(parent).addView(rows);
    return rows;
  }

  static View row(LinearLayout parent, int icon, int title, int description, Runnable action) {
    Context context = parent.getContext();
    LinearLayout row = new LinearLayout(context);
    row.setGravity(Gravity.CENTER_VERTICAL);
    row.setPadding(dp(context, 18), dp(context, 16), dp(context, 14), dp(context, 16));
    row.setMinimumHeight(dp(context, 76));
    ImageView leading = new ImageView(context);
    leading.setImageResource(icon);
    leading.setImageTintList(
        ColorStateList.valueOf(color(context, androidx.appcompat.R.attr.colorPrimary)));
    leading.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
    row.addView(leading, new LinearLayout.LayoutParams(dp(context, 24), dp(context, 24)));
    LinearLayout copy = column(context);
    LinearLayout.LayoutParams copyParams = new LinearLayout.LayoutParams(0, -2, 1);
    copyParams.setMarginStart(dp(context, 18));
    copyParams.setMarginEnd(dp(context, 12));
    row.addView(copy, copyParams);
    copy.addView(
        text(
            context,
            context.getText(title),
            com.google.android.material.R.style.TextAppearance_Material3_TitleMedium));
    if (description != 0) {
      TextView detail =
          text(
              context,
              context.getText(description),
              com.google.android.material.R.style.TextAppearance_Material3_BodyMedium);
      detail.setTextColor(color(context, com.google.android.material.R.attr.colorOnSurfaceVariant));
      detail.setPadding(0, dp(context, 4), 0, 0);
      copy.addView(detail);
    }
    if (action != null) {
      ImageView arrow = new ImageView(context);
      arrow.setImageResource(R.drawable.ic_chevron);
      arrow.setImageTintList(
          ColorStateList.valueOf(
              color(context, com.google.android.material.R.attr.colorOnSurfaceVariant)));
      arrow.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
      row.addView(arrow, new LinearLayout.LayoutParams(dp(context, 20), dp(context, 20)));
      row.setBackground(
          new RippleDrawable(
              ColorStateList.valueOf(
                  (color(context, androidx.appcompat.R.attr.colorPrimary) & 0xffffff) | 0x18000000),
              null,
              new ColorDrawable(0xffffffff)));
      row.setFocusable(true);
      row.setOnClickListener(view -> action.run());
    }
    parent.addView(row, new LinearLayout.LayoutParams(-1, -2));
    return row;
  }

  static LinearLayout actions(LinearLayout parent) {
    LinearLayout row = new LinearLayout(parent.getContext());
    row.setGravity(Gravity.CENTER_VERTICAL);
    row.setBaselineAligned(false);
    LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2);
    params.topMargin = dp(parent.getContext(), 12);
    parent.addView(row, params);
    return row;
  }

  static MaterialButton action(LinearLayout parent, int title, int icon, boolean primary) {
    Context context = parent.getContext();
    int style =
        primary
            ? R.style.ThemeOverlay_ProtectedLinux_Button_Filled
            : R.style.ThemeOverlay_ProtectedLinux_Button_Outlined;
    ContextThemeWrapper themed = new ContextThemeWrapper(context, style);
    MaterialButton button = new MaterialButton(themed);
    button.setText(title);
    button.setMinHeight(dp(context, 52));
    button.setMaxLines(3);
    button.setCornerRadius(dp(context, 18));
    if (icon != 0) {
      button.setIconResource(icon);
      button.setIconGravity(MaterialButton.ICON_GRAVITY_TEXT_START);
      button.setIconSize(dp(context, 20));
    }
    LinearLayout.LayoutParams params =
        new LinearLayout.LayoutParams(
            parent.getOrientation() == LinearLayout.HORIZONTAL ? 0 : -1,
            -2,
            parent.getOrientation() == LinearLayout.HORIZONTAL ? 1 : 0);
    if (parent.getChildCount() > 0 && parent.getOrientation() == LinearLayout.HORIZONTAL)
      params.setMarginStart(dp(context, 10));
    parent.addView(button, params);
    return button;
  }

  static MaterialButton textAction(LinearLayout parent, int title) {
    Context context = parent.getContext();
    MaterialButton button =
        new MaterialButton(
            new ContextThemeWrapper(context, R.style.ThemeOverlay_ProtectedLinux_Button_Text));
    button.setText(title);
    button.setMinHeight(dp(context, 48));
    parent.addView(button, new LinearLayout.LayoutParams(-2, -2));
    return button;
  }
}
