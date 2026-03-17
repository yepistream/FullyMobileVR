// Copyright 2020, Collabora, Ltd.
// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photone.me> ---> UI provider wiring updates so runtime entrypoints align with new ARCore config flow.
// SPDX-License-Identifier: BSL-1.0 AND AGPL-3.0-only
/*!
 * @file
 * @brief  Simple implementation of UiProvider.
 * @author Rylie Pavlik <rylie.pavlik@collabora.com>
 */

package org.freedesktop.monado.openxr_runtime

import android.app.PendingIntent
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.graphics.drawable.Icon
import android.os.Build
import dagger.hilt.android.qualifiers.ApplicationContext
import javax.inject.Inject
import org.freedesktop.monado.android_common.AboutActivity
import org.freedesktop.monado.auxiliary.UiProvider

class MonadoOpenXrUiProvider @Inject constructor(@ApplicationContext val context: Context) :
    UiProvider {

    /** Gets a drawable for use in a notification, for the runtime/Monado-incorporating target. */
    override fun getNotificationIcon(): Icon? =
        Icon.createWithResource(context, R.drawable.ic_monado_notif)

    /** Make a {@code PendingIntent} to launch an "About" activity for the runtime/target. */
    override fun makeAboutActivityPendingIntent(): PendingIntent {
        var flags = 0
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            flags = PendingIntent.FLAG_IMMUTABLE
        }
        return PendingIntent.getActivity(
            context,
            0,
            Intent.makeMainActivity(
                ComponentName.createRelative(context, AboutActivity::class.qualifiedName!!)
            ),
            flags,
        )
    }

    override fun makeConfigureActivityPendingIntent(): PendingIntent {
        var flags = 0
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            flags = PendingIntent.FLAG_IMMUTABLE
        }
        return PendingIntent.getActivity(
            context,
            1,
            Intent.makeMainActivity(
                ComponentName.createRelative(
                    context,
                    ArcoreConfigActivity::class.qualifiedName!!,
                )
            ),
            flags,
        )
    }
}
