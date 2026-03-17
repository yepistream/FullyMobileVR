// Copyright 2026, Collabora, Ltd.
// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@proton.me> ---> Main ARCore configuration activity with animated menu flow and fragment routing.
// SPDX-License-Identifier: BSL-1.0 AND AGPL-3.0-only
// Rundown: Activity shell for navigating ARCore configuration sections through animated menu transitions.
// Usage: Launched from runtime menu actions, then hosts config/setup/credits fragments inside one UI flow.
/*!
 * @file
 * @brief  ARCore runtime configuration activity.
 */
package org.freedesktop.monado.openxr_runtime

import android.animation.Animator
import android.animation.AnimatorListenerAdapter
import android.animation.AnimatorSet
import android.animation.ObjectAnimator
import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import android.view.animation.AccelerateDecelerateInterpolator
import android.view.animation.DecelerateInterpolator
import android.widget.Button
import androidx.activity.OnBackPressedCallback
import androidx.appcompat.app.AppCompatActivity
import androidx.fragment.app.commit
import dagger.hilt.android.AndroidEntryPoint

@AndroidEntryPoint
class ArcoreConfigActivity : AppCompatActivity() {

    private var isOpen = false
    private var isTransitioning = false
    private var activeBtnId: Int? = null

    private lateinit var root: View
    private lateinit var headerButton: Button
    private lateinit var headerDivider: View
    private lateinit var fragmentContainer: View
    private lateinit var menuLayer: ViewGroup

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_arcore_config)

        root = findViewById(R.id.root)
        headerButton = findViewById(R.id.headerButton)
        headerDivider = findViewById(R.id.headerDivider)
        fragmentContainer = findViewById(R.id.fragmentContainer)
        menuLayer = findViewById(R.id.menuLayer)

        headerButton.setOnClickListener {
            if (isOpen) {
                closeMenu()
            }
        }

        buttons().forEach { button -> button.setOnClickListener { onMenuClick(button) } }

        onBackPressedDispatcher.addCallback(
            this,
            object : OnBackPressedCallback(true) {
                override fun handleOnBackPressed() {
                    if (isOpen) {
                        closeMenu()
                    } else {
                        finish()
                    }
                }
            },
        )
    }

    private fun buttons(): List<Button> {
        val result = ArrayList<Button>(menuLayer.childCount)
        for (index in 0 until menuLayer.childCount) {
            val child = menuLayer.getChildAt(index)
            if (child is Button) {
                result.add(child)
            }
        }
        return result
    }

    private fun onMenuClick(clicked: Button) {
        if (root.width == 0) {
            root.post { onMenuClick(clicked) }
            return
        }
        if (isOpen || isTransitioning) {
            return
        }
        openMenu(clicked)
    }

    private fun openMenu(clicked: Button) {
        isTransitioning = true
        isOpen = true
        activeBtnId = clicked.id

        val allButtons = buttons()
        allButtons.forEach { it.isEnabled = false }
        allButtons.forEach {
            it.visibility = View.VISIBLE
            it.alpha = 1f
            it.translationX = 0f
            it.translationY = 0f
        }

        headerButton.text = clicked.text
        headerButton.visibility = View.INVISIBLE
        headerButton.alpha = 0f

        val screenWidth = root.width.toFloat()
        val clickedCenter = centerOnScreen(clicked)
        val headerCenter = centerOnScreen(headerButton)
        val dxClicked = headerCenter.first - clickedCenter.first
        val dyClicked = headerCenter.second - clickedCenter.second

        val animators = ArrayList<Animator>(allButtons.size * 3)
        animators += ObjectAnimator.ofFloat(clicked, View.TRANSLATION_X, dxClicked)
        animators += ObjectAnimator.ofFloat(clicked, View.TRANSLATION_Y, dyClicked)

        var left = true
        allButtons.filter { it != clicked }.forEach { view ->
            val center = centerOnScreen(view)
            val targetX =
                if (left) {
                    -view.width / 2f - dp(48f)
                } else {
                    screenWidth + view.width / 2f + dp(48f)
                }
            left = !left
            val dx = targetX - center.first
            animators += ObjectAnimator.ofFloat(view, View.TRANSLATION_X, dx)
            animators += ObjectAnimator.ofFloat(view, View.ALPHA, 0f)
        }

        AnimatorSet().apply {
            playTogether(animators)
            duration = 340
            interpolator = AccelerateDecelerateInterpolator()
            addListener(
                object : AnimatorListenerAdapter() {
                    override fun onAnimationEnd(animation: Animator) {
                        allButtons.filter { it != clicked }.forEach { it.visibility = View.INVISIBLE }

                        clicked.visibility = View.INVISIBLE
                        headerButton.visibility = View.VISIBLE
                        headerButton.animate().setListener(null).cancel()
                        headerButton.alpha = 1f

                        headerDivider.alpha = 0f
                        headerDivider.animate()
                            .alpha(1f)
                            .setDuration(600)
                            .setInterpolator(DecelerateInterpolator())
                            .start()

                        fragmentContainer.visibility = View.VISIBLE
                        fragmentContainer.alpha = 0f
                        fragmentContainer.translationY = 0f

                        supportFragmentManager.commit {
                            setReorderingAllowed(true)
                            replace(R.id.fragmentContainer, fragmentFor(clicked.id))
                            addToBackStack("content")
                        }

                        fragmentContainer.animate()
                            .setListener(null)
                            .alpha(1f)
                            .setDuration(900)
                            .setInterpolator(DecelerateInterpolator())
                            .start()

                        allButtons.forEach { it.isEnabled = true }
                        isTransitioning = false
                    }
                },
            )
            start()
        }
    }

    private fun closeMenu() {
        if (!isOpen || isTransitioning) {
            return
        }

        isTransitioning = true
        isOpen = false
        val allButtons = buttons()
        val active = allButtons.firstOrNull { it.id == activeBtnId }

        fragmentContainer.animate()
            .setListener(null)
            .alpha(0f)
            .translationY(0f)
            .setDuration(280)
            .setInterpolator(DecelerateInterpolator())
            .setListener(
                object : AnimatorListenerAdapter() {
                    override fun onAnimationEnd(animation: Animator) {
                        fragmentContainer.visibility = View.INVISIBLE
                        fragmentContainer.alpha = 0f
                        fragmentContainer.translationY = 0f
                        supportFragmentManager.popBackStack()
                        allButtons.forEach { it.isEnabled = true }
                        isTransitioning = false
                    }
                },
            )
            .start()

        headerButton.animate()
            .setListener(null)
            .alpha(0f)
            .setDuration(140)
            .setInterpolator(DecelerateInterpolator())
            .setListener(
                object : AnimatorListenerAdapter() {
                    override fun onAnimationEnd(animation: Animator) {
                        headerButton.visibility = View.INVISIBLE
                    }
                },
            )
            .start()

        headerDivider.animate().alpha(0f).setDuration(220).setInterpolator(DecelerateInterpolator()).start()

        allButtons.forEach {
            it.visibility = View.VISIBLE
            it.isEnabled = false
        }
        active?.visibility = View.VISIBLE

        val animators = ArrayList<Animator>(allButtons.size * 5)
        allButtons.forEach { view ->
            animators += ObjectAnimator.ofFloat(view, View.TRANSLATION_X, 0f)
            animators += ObjectAnimator.ofFloat(view, View.TRANSLATION_Y, 0f)
            animators += ObjectAnimator.ofFloat(view, View.ALPHA, 1f)
            animators += ObjectAnimator.ofFloat(view, View.SCALE_X, 1f)
            animators += ObjectAnimator.ofFloat(view, View.SCALE_Y, 1f)
        }

        AnimatorSet().apply {
            playTogether(animators)
            duration = 260
            interpolator = AccelerateDecelerateInterpolator()
            addListener(
                object : AnimatorListenerAdapter() {
                    override fun onAnimationEnd(animation: Animator) {
                        activeBtnId = null
                    }
                },
            )
            start()
        }
    }

    private fun fragmentFor(buttonId: Int) =
        when (buttonId) {
            R.id.btnA -> ArcoreHeadTrackingSettingsFragment()
            R.id.btnB -> ArcoreRuntimeSetupFragment()
            R.id.btnC -> TyToMonadoFragment()
            R.id.btnD -> ByMarkoKazimirovicFragment()
            else -> TyToMonadoFragment()
        }

    private fun centerOnScreen(view: View): Pair<Float, Float> {
        val location = IntArray(2)
        view.getLocationOnScreen(location)
        return (location[0] + view.width / 2f) to (location[1] + view.height / 2f)
    }

    private fun dp(value: Float): Float = value * resources.displayMetrics.density
}
