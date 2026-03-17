// Copyright 2020, Collabora, Ltd.
// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@proton.me> ---> Android device state/header updates for ARCore configuration and tracking data flow.
// SPDX-License-Identifier: BSL-1.0 AND AGPL-3.0-only
/*!
 * @file
 * @brief  Android sensors driver header.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_android
 */

#pragma once

#include "arcore_instance.h"

#include "math/m_api.h"
#include "math/m_clock_tracking.h"
#include "math/m_relation_history.h"

#include "xrt/xrt_device.h"

#include "os/os_threading.h"

#include "util/u_logging.h"
#include "util/u_distortion.h"



#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @implements xrt_device
 */
struct android_device
{
	struct xrt_device base;
	struct os_thread_helper oth;
	struct u_cardboard_distortion cardboard;

	struct arcore_min ar;
	struct arcore_min_config ar_cfg;
	struct m_relation_history *relation_hist;
	time_duration_ns arcore_ts_to_monotonic_ns;
	struct os_mutex camera_frame_mutex;
	bool camera_passthrough_enabled;
	struct xrt_passthrough_camera_frame camera_frame;

	enum u_logging_level log_level;
};


struct android_device *
android_device_create(void);


/*
 *
 * Printing functions.
 *
 */

#define ANDROID_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->log_level, __VA_ARGS__)
#define ANDROID_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->log_level, __VA_ARGS__)
#define ANDROID_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->log_level, __VA_ARGS__)
#define ANDROID_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->log_level, __VA_ARGS__)
#define ANDROID_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->log_level, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
