// Copyright 2026, Collabora, Ltd.
// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@proton.me> ---> ARCore head-tracking settings fragment binding UI toggles to persisted config.
// SPDX-License-Identifier: BSL-1.0 AND AGPL-3.0-only
// Rundown: Fragment containing ARCore head-tracking option controls and immediate config persistence behavior.
// Usage: Opened from the config activity to edit camera/focus/tracking-related runtime options.
/*!
 * @file
 * @brief  ARCore head-tracking settings UI bound to arcore_config.json.
 */
package org.freedesktop.monado.openxr_runtime

import android.os.Bundle
import android.view.View
import android.widget.CheckBox
import android.widget.TextView
import android.widget.Toast
import androidx.fragment.app.Fragment
import org.freedesktop.monado.android_common.RestartRuntimeDialogFragment
import org.freedesktop.monado.openxr_runtime.ArcoreConfigStore.ArcoreConfig
import org.freedesktop.monado.openxr_runtime.ArcoreConfigStore.CameraHzMode
import org.freedesktop.monado.openxr_runtime.ArcoreConfigStore.FocusMode

class ArcoreHeadTrackingSettingsFragment : Fragment(R.layout.fragment_arcore_head_tracking_settings) {

    private lateinit var store: ArcoreConfigStore
    private lateinit var config: ArcoreConfig
    private lateinit var cameraHzValue: TextView

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        store = ArcoreConfigStore(requireContext())
        config = store.loadOrCreate()

        cameraHzValue = view.findViewById(R.id.cameraHzValue)
        updateCameraHzLabel()

        view.findViewById<View>(R.id.cameraHzRow).setOnClickListener {
            config.cameraHzMode =
                if (config.cameraHzMode == CameraHzMode.MAX_ARCAMERA_HZ) {
                    CameraHzMode.MIN_ARCAMERA_HZ
                } else {
                    CameraHzMode.MAX_ARCAMERA_HZ
                }
            updateCameraHzLabel()
            persistConfig()
        }

        bindToggle(
            view = view,
            rowId = R.id.autoFocusRow,
            checkboxId = R.id.autoFocusToggle,
            initialValue = config.focusMode == FocusMode.AUTO_FOCUS_ENABLED,
        ) { enabled ->
            config.focusMode =
                if (enabled) FocusMode.AUTO_FOCUS_ENABLED else FocusMode.AUTO_FOCUS_DISABLED
            persistConfig()
        }

        bindToggle(
            view = view,
            rowId = R.id.planeDetectionRow,
            checkboxId = R.id.planeDetectionToggle,
            initialValue = config.enablePlaneDetection,
        ) { enabled ->
            config.enablePlaneDetection = enabled
            persistConfig()
        }

        bindToggle(
            view = view,
            rowId = R.id.lightEstimationRow,
            checkboxId = R.id.lightEstimationToggle,
            initialValue = config.enableLightEstimation,
        ) { enabled ->
            config.enableLightEstimation = enabled
            persistConfig()
        }

        bindToggle(
            view = view,
            rowId = R.id.depthRow,
            checkboxId = R.id.depthToggle,
            initialValue = config.enableDepth,
        ) { enabled ->
            config.enableDepth = enabled
            persistConfig()
        }

        bindToggle(
            view = view,
            rowId = R.id.instantPlacementRow,
            checkboxId = R.id.instantPlacementToggle,
            initialValue = config.enableInstantPlacement,
        ) { enabled ->
            config.enableInstantPlacement = enabled
            persistConfig()
        }

        bindToggle(
            view = view,
            rowId = R.id.augmentedFacesRow,
            checkboxId = R.id.augmentedFacesToggle,
            initialValue = config.enableAugmentedFaces,
        ) { enabled ->
            config.enableAugmentedFaces = enabled
            persistConfig()
        }

        bindToggle(
            view = view,
            rowId = R.id.imageStabilizationRow,
            checkboxId = R.id.imageStabilizationToggle,
            initialValue = config.enableImageStabilization,
        ) { enabled ->
            config.enableImageStabilization = enabled
            persistConfig()
        }

        view.findViewById<TextView>(R.id.configFileLocation).text =
            getString(R.string.arcore_config_file_location, store.getConfigFileHandle().absolutePath)

        view.findViewById<View>(R.id.restartRuntimeButton).setOnClickListener {
            RestartRuntimeDialogFragment.newInstance(getString(R.string.arcore_config_restart_required))
                .show(parentFragmentManager, "arcore_restart_dialog")
        }
    }

    private fun bindToggle(
        view: View,
        rowId: Int,
        checkboxId: Int,
        initialValue: Boolean,
        onChanged: (Boolean) -> Unit,
    ) {
        val row = view.findViewById<View>(rowId)
        val checkbox = view.findViewById<CheckBox>(checkboxId)

        checkbox.isChecked = initialValue
        row.setOnClickListener { checkbox.isChecked = !checkbox.isChecked }
        checkbox.setOnCheckedChangeListener { _, isChecked -> onChanged(isChecked) }
    }

    private fun updateCameraHzLabel() {
        val textRes =
            if (config.cameraHzMode == CameraHzMode.MAX_ARCAMERA_HZ) {
                R.string.arcore_camera_hz_max
            } else {
                R.string.arcore_camera_hz_min
            }
        cameraHzValue.text = getString(textRes)
    }

    private fun persistConfig() {
        if (!store.save(config)) {
            Toast.makeText(requireContext(), R.string.arcore_config_save_error, Toast.LENGTH_SHORT)
                .show()
        }
    }
}
