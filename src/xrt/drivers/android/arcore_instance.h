// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photone.me> ---> Minimal ARCore runtime bridge interface defining config/state used by Android tracking.
// SPDX-License-Identifier: AGPL-3.0-only
// Rundown: Declares the lightweight ARCore config/state types and lifecycle API used by Android tracking.
// Usage: Included by Android driver headers/sources to pass ARCore options and call bridge entry points.
// arcore_min.h
#pragma once

#include <jni.h>
#include <stdbool.h>
#include <stdint.h>

#include "xrt/xrt_handles.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tiny ARCore + tiny EGL pbuffer context for pose tracking.
 * Call all functions from the same thread that calls arcore_min_start_ex().
 */

enum arcore_min_focus_mode
{
	AUTO_FOCUS_DISABLED = 0,
	AUTO_FOCUS_ENABLED = 1,
};

enum arcore_min_camera_hz_mode
{
	MIN_ARCAMERA_HZ = 0,
	MAX_ARCAMERA_HZ = 1,
};

enum arcore_min_texture_update_mode
{
	ARCORE_MIN_TEXTURE_UPDATE_MODE_EXTERNAL_OES = 0,
	ARCORE_MIN_TEXTURE_UPDATE_MODE_HARDWARE_BUFFER = 1,
};

struct arcore_min_config
{
	enum arcore_min_focus_mode focus_mode;
	enum arcore_min_camera_hz_mode camera_hz_mode;
	enum arcore_min_texture_update_mode texture_update_mode;
	bool enable_plane_detection;
	bool enable_light_estimation;
	bool enable_depth;
	bool enable_instant_placement;
	bool enable_augmented_faces;
	bool enable_image_stabilization;
};

void
arcore_min_config_set_defaults(struct arcore_min_config *out_cfg);

struct arcore_min
{
	// JVM/thread stuff
	JavaVM *vm;
	jobject app_ctx_global; // GlobalRef(Context)
	bool did_attach;

	// EGL/GLES (stored as integers to keep EGL out of the header)
	uintptr_t egl_display;
	uintptr_t egl_context;
	uintptr_t egl_surface;

	uint32_t camera_tex_id; // GL_TEXTURE_EXTERNAL_OES
	enum arcore_min_texture_update_mode texture_update_mode;

	// ARCore opaque pointers
	void *session;
	void *config;
	void *frame;
	void *pose;

	bool running;
};

bool
arcore_min_start_ex(struct arcore_min *a, JavaVM *vm, jobject app_context, const struct arcore_min_config *cfg);

bool
arcore_min_start(struct arcore_min *a, JavaVM *vm, jobject app_context);

bool
arcore_min_tick(struct arcore_min *a,
                float out_pos_m[3],
                float out_rot_m[4],
                bool *out_tracking,
                int64_t *out_frame_timestamp_ns);

bool
arcore_min_get_latest_camera_frame(struct arcore_min *a,
                                   xrt_graphics_buffer_handle_t *out_handle,
                                   uint32_t *out_width,
                                   uint32_t *out_height,
                                   int64_t *out_frame_timestamp_ns);

void
arcore_min_stop(struct arcore_min *a);

static inline uint32_t
arcore_min_camera_texture_id(const struct arcore_min *a)
{
	return a ? a->camera_tex_id : 0;
}

#ifdef __cplusplus
}
#endif
