// Copyright 2026, Collabora, Ltd.
// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photone.me> ---> Credits fragment with personal links for this fork build.
// SPDX-License-Identifier: BSL-1.0 AND AGPL-3.0-only
// Rundown: Credits/identity fragment for fork-specific attribution and outbound profile links.
// Usage: Opened from the config activity credits tab to provide maintainer and project link references.
/*!
 * @file
 * @brief  Credits fragment.
 */
package org.freedesktop.monado.openxr_runtime

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.widget.TextView
import androidx.fragment.app.Fragment

class ByMarkoKazimirovicFragment : Fragment(R.layout.fragment_by_marko_kazimirovic) {

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        view.findViewById<TextView>(R.id.mkGithubRow).setOnClickListener {
            openUrl("https://github.com/yepistream")
        }
        view.findViewById<TextView>(R.id.websiteRow).setOnClickListener {
            openUrl("https://yepistream.github.io/")
        }
    }

    private fun openUrl(url: String) {
        val intent = Intent(Intent.ACTION_VIEW, Uri.parse(url))
        startActivity(intent)
    }
}
