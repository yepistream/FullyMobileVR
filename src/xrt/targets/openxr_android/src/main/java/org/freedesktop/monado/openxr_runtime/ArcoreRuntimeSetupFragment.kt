// Copyright 2026, Collabora, Ltd.
// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@proton.me> ---> Runtime setup fragment for camera/overlay permission status and request actions.
// SPDX-License-Identifier: BSL-1.0 AND AGPL-3.0-only
// Rundown: Fragment for validating and requesting Android permissions needed by the ARCore runtime flow.
// Usage: Opened from config activity runtime setup tab to guide users through camera/overlay access.
/*!
 * @file
 * @brief  Runtime permission setup fragment.
 */
package org.freedesktop.monado.openxr_runtime

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment

class ArcoreRuntimeSetupFragment : Fragment(R.layout.fragment_arcore_runtime_setup) {

    private var cameraStatusText: TextView? = null
    private var overlayStatusText: TextView? = null

    private val requestCameraPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            cameraStatusText?.setTextColor(if (granted) Color.GREEN else Color.RED)
            cameraStatusText?.text =
                if (granted) getString(R.string.arcore_permission_granted)
                else getString(R.string.arcore_permission_missing)
        }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        cameraStatusText = view.findViewById(R.id.cameraStatus)
        overlayStatusText = view.findViewById(R.id.overlayStatus)

        view.findViewById<LinearLayout>(R.id.cameraPermissionRow).setOnClickListener {
            cameraStatusText?.setTextColor(Color.YELLOW)
            requestCameraPermission.launch(Manifest.permission.CAMERA)
        }

        view.findViewById<LinearLayout>(R.id.overlayPermissionRow).setOnClickListener {
            overlayStatusText?.setTextColor(Color.YELLOW)
            openOverlayPermissionSettings()
        }

        updatePermissionUi()
    }

    override fun onResume() {
        super.onResume()
        updatePermissionUi()
    }

    private fun updatePermissionUi() {
        val cameraGranted = isCameraGranted()
        cameraStatusText?.setTextColor(if (cameraGranted) Color.GREEN else Color.RED)
        cameraStatusText?.text =
            if (cameraGranted) getString(R.string.arcore_permission_granted)
            else getString(R.string.arcore_permission_missing)

        val overlayGranted = isOverlayGranted()
        overlayStatusText?.setTextColor(if (overlayGranted) Color.GREEN else Color.RED)
        overlayStatusText?.text =
            if (overlayGranted) getString(R.string.arcore_permission_granted)
            else getString(R.string.arcore_permission_missing)
    }

    private fun isCameraGranted(): Boolean {
        return (
            ContextCompat.checkSelfPermission(requireContext(), Manifest.permission.CAMERA) ==
                PackageManager.PERMISSION_GRANTED
        )
    }

    private fun isOverlayGranted(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            Settings.canDrawOverlays(requireContext())
        } else {
            true
        }
    }

    private fun openOverlayPermissionSettings() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            val uri = Uri.parse("package:${requireContext().packageName}")
            val intent = Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, uri)
            startActivity(intent)
        }
    }
}
