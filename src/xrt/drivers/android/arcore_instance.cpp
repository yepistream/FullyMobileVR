// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photone.me> ---> Minimal ARCore runtime bridge implementation with EGL session/config control for tracking.
// SPDX-License-Identifier: AGPL-3.0-only
// Rundown: Implements a minimal ARCore-backed tracking runtime bridge for the Android driver path.
// Usage: Called by the Android sensor driver to start/configure ARCore, fetch poses, and shut down safely.

#include "arcore_instance.h"

#include <android/log.h>
#include <android/hardware_buffer.h>

#include <limits.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "arcore_c_api.h"

#define TAG "ARCORE_INSTANCE"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

static bool
arcore_config_uses_external_camera_texture(const struct arcore_min_config *cfg)
{
	return cfg != NULL && cfg->texture_update_mode == ARCORE_MIN_TEXTURE_UPDATE_MODE_EXTERNAL_OES;
}

static bool
arcore_state_uses_external_camera_texture(const struct arcore_min *a)
{
	return a != NULL && a->texture_update_mode == ARCORE_MIN_TEXTURE_UPDATE_MODE_EXTERNAL_OES;
}

static JNIEnv *
get_env(struct arcore_min *a)
{
	if (a == NULL || a->vm == NULL) {
		return NULL;
	}

	JNIEnv *env = NULL;
	jint r = (*a->vm).GetEnv((void **)&env, JNI_VERSION_1_6);
	if (r == JNI_OK) {
		return env;
	}

	if (r != JNI_EDETACHED) {
		return NULL;
	}

#if __ANDROID_API__ >= 26
	if ((*a->vm).AttachCurrentThread(&env, NULL) != JNI_OK) {
		return NULL;
	}
#else
	if ((*a->vm).AttachCurrentThread((JNIEnv **)&env, NULL) != JNI_OK) {
		return NULL;
	}
#endif

	a->did_attach = true;
	return env;
}

void
arcore_min_config_set_defaults(struct arcore_min_config *out_cfg)
{
	if (out_cfg == NULL) {
		return;
	}

	*out_cfg = (struct arcore_min_config){
	    .focus_mode = AUTO_FOCUS_DISABLED,
	    .camera_hz_mode = MAX_ARCAMERA_HZ,
	    .texture_update_mode = ARCORE_MIN_TEXTURE_UPDATE_MODE_HARDWARE_BUFFER,
	    .enable_plane_detection = false,
	    .enable_light_estimation = false,
	    .enable_depth = false,
	    .enable_instant_placement = false,
	    .enable_augmented_faces = false,
	    .enable_image_stabilization = false,
	};
}

static bool
egl_init_pbuffer(struct arcore_min *a)
{
	EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (dpy == EGL_NO_DISPLAY) {
		LOGE("eglGetDisplay failed");
		return false;
	}

	if (!eglInitialize(dpy, NULL, NULL)) {
		LOGE("eglInitialize failed: 0x%04x", (unsigned)eglGetError());
		return false;
	}

	eglBindAPI(EGL_OPENGL_ES_API);

	const EGLint cfg_attribs[] = {
	    EGL_SURFACE_TYPE,
	    EGL_PBUFFER_BIT,
	    EGL_RENDERABLE_TYPE,
	    EGL_OPENGL_ES2_BIT,
	    EGL_RED_SIZE,
	    8,
	    EGL_GREEN_SIZE,
	    8,
	    EGL_BLUE_SIZE,
	    8,
	    EGL_ALPHA_SIZE,
	    8,
	    EGL_NONE,
	};

	EGLConfig cfg = 0;
	EGLint num = 0;
	if (!eglChooseConfig(dpy, cfg_attribs, &cfg, 1, &num) || num <= 0) {
		LOGE("eglChooseConfig failed: 0x%04x", (unsigned)eglGetError());
		eglTerminate(dpy);
		return false;
	}

	const EGLint surf_attribs[] = {
	    EGL_WIDTH,
	    1,
	    EGL_HEIGHT,
	    1,
	    EGL_NONE,
	};

	EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, surf_attribs);
	if (surf == EGL_NO_SURFACE) {
		LOGE("eglCreatePbufferSurface failed: 0x%04x", (unsigned)eglGetError());
		eglTerminate(dpy);
		return false;
	}

	const EGLint ctx_attribs[] = {
	    EGL_CONTEXT_CLIENT_VERSION,
	    2,
	    EGL_NONE,
	};

	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);
	if (ctx == EGL_NO_CONTEXT) {
		LOGE("eglCreateContext failed: 0x%04x", (unsigned)eglGetError());
		eglDestroySurface(dpy, surf);
		eglTerminate(dpy);
		return false;
	}

	if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
		LOGE("eglMakeCurrent failed: 0x%04x", (unsigned)eglGetError());
		eglDestroyContext(dpy, ctx);
		eglDestroySurface(dpy, surf);
		eglTerminate(dpy);
		return false;
	}

	a->egl_display = (uintptr_t)dpy;
	a->egl_surface = (uintptr_t)surf;
	a->egl_context = (uintptr_t)ctx;
	return true;
}

static void
egl_shutdown(struct arcore_min *a)
{
	EGLDisplay dpy = (EGLDisplay)a->egl_display;
	EGLSurface surf = (EGLSurface)a->egl_surface;
	EGLContext ctx = (EGLContext)a->egl_context;

	if (dpy != EGL_NO_DISPLAY && dpy != 0) {
		eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (ctx != EGL_NO_CONTEXT && ctx != 0) {
			eglDestroyContext(dpy, ctx);
		}
		if (surf != EGL_NO_SURFACE && surf != 0) {
			eglDestroySurface(dpy, surf);
		}
		eglTerminate(dpy);
	}

	a->egl_display = 0;
	a->egl_surface = 0;
	a->egl_context = 0;
}

static bool
gl_make_external_oes_tex(uint32_t *out_tex)
{
	GLuint tex = 0;
	glGenTextures(1, &tex);
	if (tex == 0) {
		return false;
	}

	glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	*out_tex = (uint32_t)tex;
	return true;
}

static bool
arcore_camera_candidate_is_better(enum arcore_min_camera_hz_mode mode,
                                  int32_t best_fps,
                                  int64_t best_tex_area,
                                  int32_t current_fps,
                                  int64_t current_tex_area)
{
	if (best_fps < 0) {
		return true;
	}

	if (mode == MAX_ARCAMERA_HZ) {
		if (current_fps > best_fps) {
			return true;
		}
		return current_fps == best_fps && current_tex_area < best_tex_area;
	}

	if (current_fps < best_fps) {
		return true;
	}

	return current_fps == best_fps && current_tex_area < best_tex_area;
}

static bool
arcore_select_camera_config(ArSession *session, const struct arcore_min_config *cfg, ArCameraConfig **out_cfg)
{
	ArCameraConfigList *list = NULL;
	ArCameraConfigFilter *filter = NULL;
	ArCameraConfig *item = NULL;
	ArCameraConfig *selected = NULL;
	bool success = false;

	ArCameraConfigList_create(session, &list);
	ArCameraConfigFilter_create(session, &filter);

    //Had to move these declarations because the compiler was crying they were getting jumped over by goto.
    enum arcore_min_camera_hz_mode mode = cfg ? cfg->camera_hz_mode : MAX_ARCAMERA_HZ;
    int32_t best_index = -1;
    int32_t best_fps = -1;
    int64_t best_tex_area = INT64_MAX;

    int32_t count = 0;

    if (list == NULL || filter == NULL) {
		goto cleanup;
	}

	ArCameraConfigFilter_setTargetFps(
	    session, filter, AR_CAMERA_CONFIG_TARGET_FPS_30 | AR_CAMERA_CONFIG_TARGET_FPS_60);
	ArCameraConfigFilter_setDepthSensorUsage(session, filter, AR_CAMERA_CONFIG_DEPTH_SENSOR_USAGE_DO_NOT_USE);
	ArCameraConfigFilter_setStereoCameraUsage(session, filter, AR_CAMERA_CONFIG_STEREO_CAMERA_USAGE_DO_NOT_USE);
	ArCameraConfigFilter_setFacingDirection(session, filter, AR_CAMERA_CONFIG_FACING_DIRECTION_BACK);
	ArSession_getSupportedCameraConfigsWithFilter(session, filter, list);

	ArCameraConfigList_getSize(session, list, &count);
	if (count <= 0) {
		goto cleanup;
	}

	ArCameraConfig_create(session, &item);
	if (item == NULL) {
		goto cleanup;
	}



	for (int32_t i = 0; i < count; i++) {
		int32_t min_fps = 0;
		int32_t max_fps = 0;
		int32_t tex_w = 0;
		int32_t tex_h = 0;
		int64_t tex_area = 0;

		ArCameraConfigList_getItem(session, list, i, item);
		ArCameraConfig_getFpsRange(session, item, &min_fps, &max_fps);
		ArCameraConfig_getTextureDimensions(session, item, &tex_w, &tex_h);
		tex_area = (int64_t)tex_w * (int64_t)tex_h;
		(void)min_fps;

		if (!arcore_camera_candidate_is_better(mode, best_fps, best_tex_area, max_fps, tex_area)) {
			continue;
		}

		best_index = i;
		best_fps = max_fps;
		best_tex_area = tex_area;
	}

	if (best_index < 0) {
		goto cleanup;
	}

	ArCameraConfig_create(session, &selected);
	if (selected == NULL) {
		goto cleanup;
	}

	ArCameraConfigList_getItem(session, list, best_index, selected);
	*out_cfg = selected;
	selected = NULL;
	success = true;

cleanup:
	if (selected != NULL) {
		ArCameraConfig_destroy(selected);
	}
	if (item != NULL) {
		ArCameraConfig_destroy(item);
	}
	if (filter != NULL) {
		ArCameraConfigFilter_destroy(filter);
	}
	if (list != NULL) {
		ArCameraConfigList_destroy(list);
	}

	return success;
}

static void
arcore_apply_runtime_config(const ArSession *session, ArConfig *config, const struct arcore_min_config *cfg)
{
	ArPlaneFindingMode plane_mode =
	    cfg->enable_plane_detection ? AR_PLANE_FINDING_MODE_HORIZONTAL_AND_VERTICAL : AR_PLANE_FINDING_MODE_DISABLED;
	ArLightEstimationMode light_mode =
	    cfg->enable_light_estimation ? AR_LIGHT_ESTIMATION_MODE_AMBIENT_INTENSITY : AR_LIGHT_ESTIMATION_MODE_DISABLED;
	ArDepthMode depth_mode = cfg->enable_depth ? AR_DEPTH_MODE_AUTOMATIC : AR_DEPTH_MODE_DISABLED;
	ArInstantPlacementMode instant_mode =
	    cfg->enable_instant_placement ? AR_INSTANT_PLACEMENT_MODE_LOCAL_Y_UP : AR_INSTANT_PLACEMENT_MODE_DISABLED;
	ArAugmentedFaceMode face_mode =
	    cfg->enable_augmented_faces ? AR_AUGMENTED_FACE_MODE_MESH3D : AR_AUGMENTED_FACE_MODE_DISABLED;
	ArImageStabilizationMode image_stabilization_mode =
	    cfg->enable_image_stabilization ? AR_IMAGE_STABILIZATION_MODE_EIS : AR_IMAGE_STABILIZATION_MODE_OFF;
	ArFocusMode focus_mode = cfg->focus_mode == AUTO_FOCUS_ENABLED ? AR_FOCUS_MODE_AUTO : AR_FOCUS_MODE_FIXED;

	ArConfig_setUpdateMode(session, config, AR_UPDATE_MODE_LATEST_CAMERA_IMAGE);
	ArConfig_setPlaneFindingMode(session, config, plane_mode);
	ArConfig_setLightEstimationMode(session, config, light_mode);
	ArConfig_setDepthMode(session, config, depth_mode);
	ArConfig_setInstantPlacementMode(session, config, instant_mode);
	ArConfig_setAugmentedFaceMode(session, config, face_mode);
	ArConfig_setImageStabilizationMode(session, config, image_stabilization_mode);
	ArConfig_setFocusMode(session, config, focus_mode);

	if (arcore_config_uses_external_camera_texture(cfg)) {
		ArConfig_setTextureUpdateMode(session, config, AR_TEXTURE_UPDATE_MODE_BIND_TO_TEXTURE_EXTERNAL_OES);
	} else {
		ArConfig_setTextureUpdateMode(session, config, AR_TEXTURE_UPDATE_MODE_EXPOSE_HARDWARE_BUFFER);
	}
}

static void
arcore_cleanup_after_failed_start(struct arcore_min *a,
                                  JNIEnv *env,
                                  ArSession *session,
                                  ArConfig *config,
                                  ArFrame *frame,
                                  ArPose *pose)
{
	if (pose != NULL) {
		ArPose_destroy(pose);
	}
	if (frame != NULL) {
		ArFrame_destroy(frame);
	}
	if (config != NULL) {
		ArConfig_destroy(config);
	}
	if (session != NULL) {
		ArSession_destroy(session);
	}

	if (arcore_state_uses_external_camera_texture(a)) {
		if (a->camera_tex_id != 0) {
			glDeleteTextures(1, (GLuint *)&a->camera_tex_id);
			a->camera_tex_id = 0;
		}
		egl_shutdown(a);
	}

	if (env != NULL && a->app_ctx_global != NULL) {
		(*env).DeleteGlobalRef(a->app_ctx_global);
		a->app_ctx_global = NULL;
	}

	if (a->did_attach && a->vm != NULL) {
		(*a->vm).DetachCurrentThread();
	}

	memset(a, 0, sizeof(*a));
}

bool
arcore_min_start_ex(struct arcore_min *a, JavaVM *vm, jobject app_context, const struct arcore_min_config *cfg)
{
	if (a == NULL || vm == NULL || app_context == NULL) {
		return false;
	}

	struct arcore_min_config effective_cfg;
	arcore_min_config_set_defaults(&effective_cfg);
	if (cfg != NULL) {
		effective_cfg = *cfg;
	}

	memset(a, 0, sizeof(*a));
	a->vm = vm;
	a->texture_update_mode = effective_cfg.texture_update_mode;

	bool use_external_camera_texture = arcore_config_uses_external_camera_texture(&effective_cfg);

	JNIEnv *env = get_env(a);
	if (env == NULL) {
		LOGE("get_env failed");
		return false;
	}

	a->app_ctx_global = (*env).NewGlobalRef(app_context);
	if (a->app_ctx_global == NULL) {
		LOGE("NewGlobalRef(app_context) failed");
		arcore_cleanup_after_failed_start(a, env, NULL, NULL, NULL, NULL);
		return false;
	}

	if (use_external_camera_texture) {
		if (!egl_init_pbuffer(a)) {
			LOGE("EGL init failed");
			arcore_cleanup_after_failed_start(a, env, NULL, NULL, NULL, NULL);
			return false;
		}
		if (!gl_make_external_oes_tex(&a->camera_tex_id)) {
			LOGE("External OES texture creation failed");
			arcore_cleanup_after_failed_start(a, env, NULL, NULL, NULL, NULL);
			return false;
		}
	}

	ArSession *session = NULL;
	ArConfig *config = NULL;
	ArFrame *frame = NULL;
	ArPose *pose = NULL;
	ArStatus st = ArSession_create(env, a->app_ctx_global, &session);
	if (st != AR_SUCCESS || session == NULL) {
		LOGE("ArSession_create failed: %d", (int)st);
		arcore_cleanup_after_failed_start(a, env, session, config, frame, pose);
		return false;
	}

	ArConfig_create(session, &config);
	if (config == NULL) {
		LOGE("ArConfig_create failed");
		arcore_cleanup_after_failed_start(a, env, session, config, frame, pose);
		return false;
	}

	ArCameraConfig *selected_cfg = NULL;
	if (arcore_select_camera_config(session, &effective_cfg, &selected_cfg) && selected_cfg != NULL) {
		st = ArSession_setCameraConfig(session, selected_cfg);
		if (st != AR_SUCCESS) {
			LOGW("ArSession_setCameraConfig failed: %d", (int)st);
		}
		ArCameraConfig_destroy(selected_cfg);
	} else {
		LOGW("No ARCore camera config matched filter, using ARCore default");
	}

	arcore_apply_runtime_config(session, config, &effective_cfg);
	st = ArSession_configure(session, config);
	if (st != AR_SUCCESS) {
		LOGE("ArSession_configure failed: %d", (int)st);
		arcore_cleanup_after_failed_start(a, env, session, config, frame, pose);
		return false;
	}

	if (use_external_camera_texture) {
		ArSession_setCameraTextureName(session, (int32_t)a->camera_tex_id);
	}

	ArFrame_create(session, &frame);
	ArPose_create(session, NULL, &pose);
	if (frame == NULL || pose == NULL) {
		LOGE("Failed creating ARCore frame/pose objects");
		arcore_cleanup_after_failed_start(a, env, session, config, frame, pose);
		return false;
	}

	st = ArSession_resume(session);
	if (st != AR_SUCCESS) {
		LOGE("ArSession_resume failed: %d", (int)st);
		arcore_cleanup_after_failed_start(a, env, session, config, frame, pose);
		return false;
	}

	a->session = session;
	a->config = config;
	a->frame = frame;
	a->pose = pose;
	a->running = true;

	LOGI("ARCore started (tex=%u, texture_update_mode=%d)", a->camera_tex_id, (int)a->texture_update_mode);
	return true;
}

bool
arcore_min_start(struct arcore_min *a, JavaVM *vm, jobject app_context)
{
	struct arcore_min_config cfg;
	arcore_min_config_set_defaults(&cfg);
	return arcore_min_start_ex(a, vm, app_context, &cfg);
}

static bool
arcore_extract_tracking_pose(const ArSession *session,
                            const ArCamera *camera,
                            ArPose *pose,
                            float out_pos_m[3],
                            float out_rot_m[4])
{
	ArTrackingState tracking_state = AR_TRACKING_STATE_STOPPED;
	ArCamera_getTrackingState(session, camera, &tracking_state);
	if (tracking_state != AR_TRACKING_STATE_TRACKING) {
		return false;
	}

	float raw[7] = {0};
	ArCamera_getPose(session, camera, pose);
	ArPose_getPoseRaw(session, pose, raw);

	if (out_pos_m != NULL) {
		memcpy(out_pos_m, raw + 4, sizeof(float) * 3);
	}
	if (out_rot_m != NULL) {
		memcpy(out_rot_m, raw, sizeof(float) * 4);
	}

	return true;
}

bool
arcore_min_tick(struct arcore_min *a,
                float out_pos_m[3],
                float out_rot_m[4],
                bool *out_tracking,
                int64_t *out_frame_timestamp_ns)
{
	if (a == NULL || !a->running || a->session == NULL || a->frame == NULL || a->pose == NULL) {
		return false;
	}

	if (out_tracking != NULL) {
		*out_tracking = false;
	}
	if (out_frame_timestamp_ns != NULL) {
		*out_frame_timestamp_ns = 0;
	}

	if (arcore_state_uses_external_camera_texture(a)) {
		EGLDisplay dpy = (EGLDisplay)a->egl_display;
		EGLSurface surf = (EGLSurface)a->egl_surface;
		EGLContext ctx = (EGLContext)a->egl_context;
		if (dpy == 0 || surf == 0 || ctx == 0) {
			return false;
		}
		if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
			return false;
		}
	}

	ArSession *session = (ArSession *)a->session;
	ArFrame *frame = (ArFrame *)a->frame;
	ArPose *pose = (ArPose *)a->pose;

	if (ArSession_update(session, frame) != AR_SUCCESS) {
		return false;
	}

	if (out_frame_timestamp_ns != NULL) {
		ArFrame_getTimestamp(session, frame, out_frame_timestamp_ns);
	}

	ArCamera *camera = NULL;
	ArFrame_acquireCamera(session, frame, &camera);
	if (camera == NULL) {
		return true;
	}

	bool tracked = arcore_extract_tracking_pose(session, camera, pose, out_pos_m, out_rot_m);
	if (out_tracking != NULL) {
		*out_tracking = tracked;
	}

	ArCamera_release(camera);
	return true;
}

bool
arcore_min_get_latest_camera_frame(struct arcore_min *a,
                                   xrt_graphics_buffer_handle_t *out_handle,
                                   uint32_t *out_width,
                                   uint32_t *out_height,
                                   int64_t *out_frame_timestamp_ns)
{
	if (a == NULL || !a->running || a->session == NULL || a->frame == NULL || out_handle == NULL) {
		return false;
	}

	if (arcore_state_uses_external_camera_texture(a)) {
		return false;
	}

	*out_handle = XRT_GRAPHICS_BUFFER_HANDLE_INVALID;

	ArSession *session = (ArSession *)a->session;
	ArFrame *frame = (ArFrame *)a->frame;

	AHardwareBuffer *buffer = NULL;
	ArStatus st = ArFrame_getHardwareBuffer(session, frame, reinterpret_cast<void **>(&buffer));
	if (st != AR_SUCCESS || buffer == NULL) {
		return false;
	}

	AHardwareBuffer_acquire(buffer);

	if (out_frame_timestamp_ns != NULL) {
		ArFrame_getTimestamp(session, frame, out_frame_timestamp_ns);
	}

	if (out_width != NULL || out_height != NULL) {
		AHardwareBuffer_Desc desc = {0};
		AHardwareBuffer_describe(buffer, &desc);
		if (out_width != NULL) {
			*out_width = desc.width;
		}
		if (out_height != NULL) {
			*out_height = desc.height;
		}
	}

	*out_handle = buffer;
	return true;
}

void
arcore_min_stop(struct arcore_min *a)
{
	if (a == NULL) {
		return;
	}

	JNIEnv *env = get_env(a);

	if (a->session != NULL) {
		ArSession_pause((ArSession *)a->session);
	}
	if (a->pose != NULL) {
		ArPose_destroy((ArPose *)a->pose);
		a->pose = NULL;
	}
	if (a->frame != NULL) {
		ArFrame_destroy((ArFrame *)a->frame);
		a->frame = NULL;
	}
	if (a->config != NULL) {
		ArConfig_destroy((ArConfig *)a->config);
		a->config = NULL;
	}
	if (a->session != NULL) {
		ArSession_destroy((ArSession *)a->session);
		a->session = NULL;
	}

	if (arcore_state_uses_external_camera_texture(a)) {
		if (a->camera_tex_id != 0) {
			glDeleteTextures(1, (GLuint *)&a->camera_tex_id);
			a->camera_tex_id = 0;
		}
		egl_shutdown(a);
	}

	if (env != NULL && a->app_ctx_global != NULL) {
		(*env).DeleteGlobalRef(a->app_ctx_global);
		a->app_ctx_global = NULL;
	}
	if (a->did_attach && a->vm != NULL) {
		(*a->vm).DetachCurrentThread();
	}

	memset(a, 0, sizeof(*a));
}
