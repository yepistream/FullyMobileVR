// Copyright 2026, Collabora, Ltd.
// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@proton.me> ---> Embedded Monado about/thanks fragment integration within the new config UI shell.
// SPDX-License-Identifier: BSL-1.0 AND AGPL-3.0-only
// Rundown: Fragment wrapper embedding Monado acknowledgement/about content in the fork configuration flow.
// Usage: Opened from config activity “thanks” section to retain upstream attribution and notices in-app.
/*!
 * @file
 * @brief  Shows the original Monado about layout inside the integrated config UI.
 */
package org.freedesktop.monado.openxr_runtime

import android.os.Bundle
import android.os.Process
import android.text.method.LinkMovementMethod
import android.view.View
import android.widget.Button
import android.widget.ImageView
import android.widget.TextView
import androidx.fragment.app.Fragment
import dagger.hilt.android.AndroidEntryPoint
import javax.inject.Inject
import org.freedesktop.monado.android_common.DisplayOverOtherAppsStatusFragment
import org.freedesktop.monado.android_common.NoticeFragmentProvider
import org.freedesktop.monado.android_common.VrModeStatus
import org.freedesktop.monado.auxiliary.NameAndLogoProvider

@AndroidEntryPoint
class TyToMonadoFragment : Fragment(R.layout.activity_about) {

    @Inject lateinit var noticeFragmentProvider: NoticeFragmentProvider

    @Inject lateinit var nameAndLogoProvider: NameAndLogoProvider

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        view.findViewById<TextView>(R.id.textPowered).movementMethod = LinkMovementMethod.getInstance()
        view.findViewById<TextView>(R.id.textName).text = nameAndLogoProvider.getLocalizedRuntimeName()
        view.findViewById<ImageView>(R.id.imageView).setImageDrawable(nameAndLogoProvider.getLogoDrawable())

        if (!isInProcessBuild()) {
            val shutdownButton = view.findViewById<Button>(R.id.shutdown)
            shutdownButton.visibility = View.VISIBLE
            shutdownButton.setOnClickListener { Process.killProcess(Process.myPid()) }
        }

        if (savedInstanceState != null) {
            return
        }

        val transaction = childFragmentManager.beginTransaction()

        @VrModeStatus.Status
        val status = VrModeStatus.detectStatus(requireContext(), requireContext().packageName)
        transaction.add(R.id.statusFrame, VrModeStatus.newInstance(status), null)

        if (!isInProcessBuild()) {
            view.findViewById<View>(R.id.drawOverOtherAppsFrame).visibility = View.VISIBLE
            transaction.replace(
                R.id.drawOverOtherAppsFrame,
                DisplayOverOtherAppsStatusFragment(),
                null,
            )
        }

        transaction.add(R.id.aboutFrame, noticeFragmentProvider.makeNoticeFragment(), null)
        transaction.commit()
    }

    private fun isInProcessBuild(): Boolean {
        return try {
            javaClass.classLoader?.loadClass("org.freedesktop.monado.ipc.Client")
            false
        } catch (_: ClassNotFoundException) {
            true
        }
    }
}
