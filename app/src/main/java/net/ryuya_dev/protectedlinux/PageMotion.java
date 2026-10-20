package net.ryuya_dev.protectedlinux;

import android.animation.ValueAnimator;
import android.app.Activity;
import android.app.ActivityOptions;
import android.content.Intent;
import android.view.*;
import androidx.core.view.ViewCompat;
import androidx.transition.Transition;
import androidx.transition.TransitionManager;
import com.google.android.material.transition.MaterialFadeThrough;
import com.google.android.material.transition.MaterialSharedAxis;

/** Material navigation motion, restricted to page containers rather than their live contents. */
final class PageMotion {
  private PageMotion() {}

  static void change(
      ViewGroup root,
      View outgoing,
      View incoming,
      boolean hierarchy,
      boolean forward,
      Runnable update) {
    // Finish an interrupted transition before capturing the next navigation state.
    // This also releases disappearing views when the user taps tabs rapidly.
    TransitionManager.endTransitions(root);
    if (outgoing != incoming && ViewCompat.isLaidOut(root) && ValueAnimator.areAnimatorsEnabled()) {
      Transition transition =
          hierarchy
              ? new MaterialSharedAxis(MaterialSharedAxis.X, forward)
              : new MaterialFadeThrough();
      transition.addTarget(outgoing);
      transition.addTarget(incoming);
      TransitionManager.beginDelayedTransition(root, transition);
    }
    update.run();
  }

  static void configureActivity(Activity activity, ViewGroup content) {
    // Window transitions must capture the same group whose visibility Android
    // changes. Targeting android.R.id.content alone leaves both pages unanimated.
    content.setId(R.id.page_transition_container);
    content.setTransitionGroup(true);
    content.setBackgroundColor(
        UiKit.color(activity, com.google.android.material.R.attr.colorSurface));
    android.transition.Transition enter = axis(true), leave = axis(false);
    activity.getWindow().setEnterTransition(enter);
    activity.getWindow().setExitTransition(axis(true));
    activity.getWindow().setReturnTransition(leave);
    activity.getWindow().setReenterTransition(axis(false));
    activity.getWindow().setAllowEnterTransitionOverlap(true);
    activity.getWindow().setAllowReturnTransitionOverlap(true);
  }

  private static android.transition.Transition axis(boolean forward) {
    var transition =
        new com.google.android.material.transition.platform.MaterialSharedAxis(
            com.google.android.material.transition.platform.MaterialSharedAxis.X, forward);
    // System bars stay owned by Android; animate only this activity's content.
    transition.addTarget(R.id.page_transition_container);
    return transition;
  }

  static void open(Activity source, Class<? extends Activity> target) {
    Intent intent = new Intent(source, target);
    if (ValueAnimator.areAnimatorsEnabled()) {
      source.startActivity(intent, ActivityOptions.makeSceneTransitionAnimation(source).toBundle());
    } else {
      source.startActivity(intent);
    }
  }
}
