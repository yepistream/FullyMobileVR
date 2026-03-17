// Copyright 2026, Collabora, Ltd.
// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photone.me> ---> ARCore JSON config storage/parsing layer with defaults and typed option mapping.
// SPDX-License-Identifier: BSL-1.0 AND AGPL-3.0-only
// Rundown: Configuration storage helper that reads/writes runtime ARCore settings in JSON form.
// Usage: Used by ARCore settings fragments to persist user choices and reload defaults on app start.
/*!
 * @file
 * @brief  Storage and parsing for ARCore configuration settings.
 */
package org.freedesktop.monado.openxr_runtime

import android.content.Context
import java.io.File
import java.io.IOException
import java.util.Locale
import org.json.JSONObject

class ArcoreConfigStore(private val context: Context) {

    data class ArcoreConfig(
        var focusMode: FocusMode = FocusMode.AUTO_FOCUS_DISABLED,
        var cameraHzMode: CameraHzMode = CameraHzMode.MAX_ARCAMERA_HZ,
        var enablePlaneDetection: Boolean = false,
        var enableLightEstimation: Boolean = false,
        var enableDepth: Boolean = false,
        var enableInstantPlacement: Boolean = false,
        var enableAugmentedFaces: Boolean = false,
        var enableImageStabilization: Boolean = false,
    )

    enum class FocusMode(val configValue: String) {
        AUTO_FOCUS_DISABLED("AUTO_FOCUS_DISABLED"),
        AUTO_FOCUS_ENABLED("AUTO_FOCUS_ENABLED"),
    }

    enum class CameraHzMode(val configValue: String) {
        MIN_ARCAMERA_HZ("MIN_ARCAMERA_HZ"),
        MAX_ARCAMERA_HZ("MAX_ARCAMERA_HZ"),
    }

    private val configFile by lazy(LazyThreadSafetyMode.NONE) { File(context.filesDir, CONFIG_FILE) }
    private val lock = Any()

    fun getConfigFileHandle(): File = configFile

    fun loadOrCreate(): ArcoreConfig =
        synchronized(lock) {
            if (!configFile.exists()) {
                val defaults = ArcoreConfig()
                saveLocked(defaults)
                return defaults
            }

            return try {
                parseConfig(configFile.readText())
            } catch (ignored: Exception) {
                ArcoreConfig()
            }
        }

    fun save(config: ArcoreConfig): Boolean = synchronized(lock) { saveLocked(config) }

    private fun saveLocked(config: ArcoreConfig): Boolean {
        return try {
            configFile.parentFile?.mkdirs()
            configFile.writeText(config.toJson().toString(2))
            true
        } catch (ignored: IOException) {
            false
        }
    }

    private fun parseConfig(rawJson: String): ArcoreConfig {
        val json = JSONObject(rawJson)
        val parsed = ArcoreConfig()

        parseFocusMode(json.opt(KEY_FOCUS_MODE))?.let { parsed.focusMode = it }
        parseCameraHzMode(json.opt(KEY_CAMERA_HZ_MODE))?.let { parsed.cameraHzMode = it }
        parseBool(json.opt(KEY_ENABLE_PLANE_DETECTION))?.let { parsed.enablePlaneDetection = it }
        parseBool(json.opt(KEY_ENABLE_LIGHT_ESTIMATION))?.let { parsed.enableLightEstimation = it }
        parseBool(json.opt(KEY_ENABLE_DEPTH))?.let { parsed.enableDepth = it }
        parseBool(json.opt(KEY_ENABLE_INSTANT_PLACEMENT))?.let { parsed.enableInstantPlacement = it }
        parseBool(json.opt(KEY_ENABLE_AUGMENTED_FACES))?.let { parsed.enableAugmentedFaces = it }
        parseBool(json.opt(KEY_ENABLE_IMAGE_STABILIZATION))?.let { parsed.enableImageStabilization = it }

        return parsed
    }

    private fun parseFocusMode(value: Any?): FocusMode? =
        when (value) {
            is Number ->
                when (value.toInt()) {
                    0 -> FocusMode.AUTO_FOCUS_DISABLED
                    1 -> FocusMode.AUTO_FOCUS_ENABLED
                    else -> null
                }
            is String ->
                when (value.trim().lowercase(Locale.ROOT)) {
                    "auto_focus_disabled", "disabled", "fixed", "off" ->
                        FocusMode.AUTO_FOCUS_DISABLED
                    "auto_focus_enabled", "enabled", "auto", "on" -> FocusMode.AUTO_FOCUS_ENABLED
                    else -> null
                }
            else -> null
        }

    private fun parseCameraHzMode(value: Any?): CameraHzMode? =
        when (value) {
            is Number ->
                when (value.toInt()) {
                    0 -> CameraHzMode.MIN_ARCAMERA_HZ
                    1 -> CameraHzMode.MAX_ARCAMERA_HZ
                    else -> null
                }
            is String ->
                when (value.trim().lowercase(Locale.ROOT)) {
                    "min_arcamera_hz", "min", "low" -> CameraHzMode.MIN_ARCAMERA_HZ
                    "max_arcamera_hz", "max", "high" -> CameraHzMode.MAX_ARCAMERA_HZ
                    else -> null
                }
            else -> null
        }

    private fun parseBool(value: Any?): Boolean? =
        when (value) {
            is Boolean -> value
            is Number ->
                when (value.toInt()) {
                    0 -> false
                    1 -> true
                    else -> null
                }
            is String ->
                when (value.trim().lowercase(Locale.ROOT)) {
                    "false", "0" -> false
                    "true", "1" -> true
                    else -> null
                }
            else -> null
        }

    private fun ArcoreConfig.toJson(): JSONObject {
        val json = JSONObject()
        json.put(KEY_FOCUS_MODE, focusMode.configValue)
        json.put(KEY_CAMERA_HZ_MODE, cameraHzMode.configValue)
        json.put(KEY_ENABLE_PLANE_DETECTION, enablePlaneDetection)
        json.put(KEY_ENABLE_LIGHT_ESTIMATION, enableLightEstimation)
        json.put(KEY_ENABLE_DEPTH, enableDepth)
        json.put(KEY_ENABLE_INSTANT_PLACEMENT, enableInstantPlacement)
        json.put(KEY_ENABLE_AUGMENTED_FACES, enableAugmentedFaces)
        json.put(KEY_ENABLE_IMAGE_STABILIZATION, enableImageStabilization)
        return json
    }

    companion object {
        private const val CONFIG_FILE = "arcore_config.json"
        private const val KEY_FOCUS_MODE = "focus_mode"
        private const val KEY_CAMERA_HZ_MODE = "camera_hz_mode"
        private const val KEY_ENABLE_PLANE_DETECTION = "enable_plane_detection"
        private const val KEY_ENABLE_LIGHT_ESTIMATION = "enable_light_estimation"
        private const val KEY_ENABLE_DEPTH = "enable_depth"
        private const val KEY_ENABLE_INSTANT_PLACEMENT = "enable_instant_placement"
        private const val KEY_ENABLE_AUGMENTED_FACES = "enable_augmented_faces"
        private const val KEY_ENABLE_IMAGE_STABILIZATION = "enable_image_stabilization"
    }
}
