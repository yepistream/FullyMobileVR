// Copyright 2013, Fredrik Hultin.
// Copyright 2013, Jakob Bornecrantz.
// Copyright 2015, Joey Ferwerda.
// Copyright 2020-2025, Collabora, Ltd.
// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photone.me> ---> ARCore config loading, camera mode controls, and sensor timing integration updates.
// SPDX-License-Identifier: BSL-1.0 AND AGPL-3.0-only
/*!
 * @file
 * @brief  Android sensors driver code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 * @author Simon Zeni <simon.zeni@collabora.com>
 * @ingroup drv_android
 */

#include "android_sensors.h"

#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_distortion_mesh.h"
#include "util/u_file.h"
#include "util/u_handles.h"
#include "util/u_var.h"
#include "util/u_visibility_mask.h"

#include "cardboard_device.pb.h"
#include "pb_decode.h"

#include "android/android_globals.h"
#include "android/android_content.h"
#include "android/android_custom_surface.h"

#include <cjson/cJSON.h>

#include <android/looper.h>
#include <android/sensor.h>

#include <xrt/xrt_config_android.h>

#include "os/os_time.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Workaround to avoid the inclusion of "android_native_app_glue.h.
#ifndef LOOPER_ID_USER
#define LOOPER_ID_USER 3
#endif

// 60 events per second (in us).
#define POLL_RATE_USEC (1000L / 60) * 1000
#define MIN_SLEEP_USEC 1000
#define MAX_SLEEP_USEC 50000
#define ARCORE_SAMPLE_HZ_MIN 15.0f
#define ARCORE_SAMPLE_HZ_MAX 120.0f
#define ARCORE_SAMPLE_HZ_SMOOTHING 0.10f
#define GYRO_BIAS_TRACK_ALPHA 0.005f
#define GYRO_BIAS_TRACK_MAX_RAD_S 0.35f
#define GYRO_SPIKE_CLAMP_RAD_S 20.0f
#define ARCORE_CONFIG_JSON_FILENAME "arcore_config.json"
#define ARCORE_CONFIG_ENV_VAR "MONADO_ARCORE_CONFIG_JSON"


DEBUG_GET_ONCE_LOG_OPTION(android_log, "ANDROID_SENSORS_LOG", U_LOGGING_WARN)

struct decode_buffer
{
	uint8_t *data;
	size_t capacity;
	size_t used;
	bool nul_terminate;
};

struct android_gyro_sample
{
	struct xrt_vec3 angular_velocity_device;
	int64_t timestamp_ns;
	bool valid;
};

struct android_gyro_stream
{
	ASensorManager *manager;
	const ASensor *sensor;
	ASensorEventQueue *queue;
	ALooper *looper;
	struct android_gyro_sample latest;
	struct xrt_vec3 bias_device;
	bool have_bias;
};

static const char *
android_arcore_focus_mode_to_string(enum arcore_min_focus_mode mode)
{
	switch (mode) {
	case AUTO_FOCUS_DISABLED: return "AUTO_FOCUS_DISABLED";
	case AUTO_FOCUS_ENABLED: return "AUTO_FOCUS_ENABLED";
	default: return "AUTO_FOCUS_DISABLED";
	}
}

static const char *
android_arcore_camera_hz_mode_to_string(enum arcore_min_camera_hz_mode mode)
{
	switch (mode) {
	case MIN_ARCAMERA_HZ: return "MIN_ARCAMERA_HZ";
	case MAX_ARCAMERA_HZ: return "MAX_ARCAMERA_HZ";
	default: return "MAX_ARCAMERA_HZ";
	}
}

static const char *
android_arcore_texture_update_mode_to_string(enum arcore_min_texture_update_mode mode)
{
	switch (mode) {
	case ARCORE_MIN_TEXTURE_UPDATE_MODE_EXTERNAL_OES: return "EXTERNAL_OES";
	case ARCORE_MIN_TEXTURE_UPDATE_MODE_HARDWARE_BUFFER: return "HARDWARE_BUFFER";
	default: return "HARDWARE_BUFFER";
	}
}

static bool
android_write_arcore_config_file(const char *path, const struct arcore_min_config *cfg)
{
	FILE *file = fopen(path, "wb");
	if (file == NULL) {
		return false;
	}

	int ret = fprintf(file,                                                  //
	                  "{\n"                                                  //
	                  "  \"focus_mode\": \"%s\",\n"                          //
	                  "  \"camera_hz_mode\": \"%s\",\n"                      //
	                  "  \"texture_update_mode\": \"%s\",\n"                 //
	                  "  \"enable_plane_detection\": %s,\n"                  //
	                  "  \"enable_light_estimation\": %s,\n"                 //
	                  "  \"enable_depth\": %s,\n"                            //
	                  "  \"enable_instant_placement\": %s,\n"                //
	                  "  \"enable_augmented_faces\": %s,\n"                  //
	                  "  \"enable_image_stabilization\": %s\n"               //
	                  "}\n",                                                 //
	                  android_arcore_focus_mode_to_string(cfg->focus_mode),  //
	                  android_arcore_camera_hz_mode_to_string(cfg->camera_hz_mode),
	                  android_arcore_texture_update_mode_to_string(cfg->texture_update_mode),
	                  cfg->enable_plane_detection ? "true" : "false",
	                  cfg->enable_light_estimation ? "true" : "false", cfg->enable_depth ? "true" : "false",
	                  cfg->enable_instant_placement ? "true" : "false",
	                  cfg->enable_augmented_faces ? "true" : "false",
	                  cfg->enable_image_stabilization ? "true" : "false");
	(void)fclose(file);

	return ret > 0;
}

static bool
android_parse_json_bool(const cJSON *item, bool *out_value)
{
	if (item == NULL || out_value == NULL) {
		return false;
	}

	if (cJSON_IsBool(item)) {
		*out_value = cJSON_IsTrue(item);
		return true;
	}

	if (cJSON_IsNumber(item)) {
		*out_value = cJSON_GetNumberValue(item) != 0.0;
		return true;
	}

	return false;
}

static bool
android_parse_json_focus_mode(const cJSON *item, enum arcore_min_focus_mode *out_mode)
{
	if (item == NULL || out_mode == NULL) {
		return false;
	}

	if (cJSON_IsNumber(item)) {
		int mode = (int)cJSON_GetNumberValue(item);
		if (mode == AUTO_FOCUS_DISABLED || mode == AUTO_FOCUS_ENABLED) {
			*out_mode = (enum arcore_min_focus_mode)mode;
			return true;
		}
		return false;
	}

	if (!cJSON_IsString(item) || item->valuestring == NULL) {
		return false;
	}

	const char *value = item->valuestring;
	if (strcmp(value, "AUTO_FOCUS_DISABLED") == 0 || strcmp(value, "disabled") == 0 || strcmp(value, "fixed") == 0 ||
	    strcmp(value, "off") == 0) {
		*out_mode = AUTO_FOCUS_DISABLED;
		return true;
	}

	if (strcmp(value, "AUTO_FOCUS_ENABLED") == 0 || strcmp(value, "enabled") == 0 || strcmp(value, "auto") == 0 ||
	    strcmp(value, "on") == 0) {
		*out_mode = AUTO_FOCUS_ENABLED;
		return true;
	}

	return false;
}

static bool
android_parse_json_texture_update_mode(const cJSON *item, enum arcore_min_texture_update_mode *out_mode)
{
	if (item == NULL || out_mode == NULL) {
		return false;
	}

	if (cJSON_IsNumber(item)) {
		int mode = (int)cJSON_GetNumberValue(item);
		if (mode == ARCORE_MIN_TEXTURE_UPDATE_MODE_EXTERNAL_OES ||
		    mode == ARCORE_MIN_TEXTURE_UPDATE_MODE_HARDWARE_BUFFER) {
			*out_mode = (enum arcore_min_texture_update_mode)mode;
			return true;
		}
		return false;
	}

	if (!cJSON_IsString(item) || item->valuestring == NULL) {
		return false;
	}

	const char *value = item->valuestring;
	if (strcmp(value, "EXTERNAL_OES") == 0 || strcmp(value, "external_oes") == 0 || strcmp(value, "external") == 0 ||
	    strcmp(value, "oes") == 0) {
		*out_mode = ARCORE_MIN_TEXTURE_UPDATE_MODE_EXTERNAL_OES;
		return true;
	}

	if (strcmp(value, "HARDWARE_BUFFER") == 0 || strcmp(value, "hardware_buffer") == 0 ||
	    strcmp(value, "hardware") == 0 || strcmp(value, "ahardwarebuffer") == 0 ||
	    strcmp(value, "ahardware_buffer") == 0 || strcmp(value, "ahb") == 0) {
		*out_mode = ARCORE_MIN_TEXTURE_UPDATE_MODE_HARDWARE_BUFFER;
		return true;
	}

	return false;
}

static bool
android_parse_json_camera_hz_mode(const cJSON *item, enum arcore_min_camera_hz_mode *out_mode)
{
	if (item == NULL || out_mode == NULL) {
		return false;
	}

	if (cJSON_IsNumber(item)) {
		int mode = (int)cJSON_GetNumberValue(item);
		if (mode == MIN_ARCAMERA_HZ || mode == MAX_ARCAMERA_HZ) {
			*out_mode = (enum arcore_min_camera_hz_mode)mode;
			return true;
		}
		return false;
	}

	if (!cJSON_IsString(item) || item->valuestring == NULL) {
		return false;
	}

	const char *value = item->valuestring;
	if (strcmp(value, "MIN_ARCAMERA_HZ") == 0 || strcmp(value, "min") == 0 || strcmp(value, "low") == 0) {
		*out_mode = MIN_ARCAMERA_HZ;
		return true;
	}

	if (strcmp(value, "MAX_ARCAMERA_HZ") == 0 || strcmp(value, "max") == 0 || strcmp(value, "high") == 0) {
		*out_mode = MAX_ARCAMERA_HZ;
		return true;
	}

	return false;
}

static void
android_apply_arcore_json_config(struct android_device *d, const cJSON *root, struct arcore_min_config *inout_cfg)
{
	const cJSON *item = NULL;

	item = cJSON_GetObjectItemCaseSensitive(root, "focus_mode");
	if (item != NULL && !android_parse_json_focus_mode(item, &inout_cfg->focus_mode)) {
		ANDROID_WARN(d, "Ignoring invalid 'focus_mode' in ARCore config");
	}

	item = cJSON_GetObjectItemCaseSensitive(root, "camera_hz_mode");
	if (item != NULL && !android_parse_json_camera_hz_mode(item, &inout_cfg->camera_hz_mode)) {
		ANDROID_WARN(d, "Ignoring invalid 'camera_hz_mode' in ARCore config");
	}

	item = cJSON_GetObjectItemCaseSensitive(root, "texture_update_mode");
	if (item != NULL && !android_parse_json_texture_update_mode(item, &inout_cfg->texture_update_mode)) {
		ANDROID_WARN(d, "Ignoring invalid 'texture_update_mode' in ARCore config");
	}

	item = cJSON_GetObjectItemCaseSensitive(root, "enable_plane_detection");
	if (item != NULL && !android_parse_json_bool(item, &inout_cfg->enable_plane_detection)) {
		ANDROID_WARN(d, "Ignoring invalid 'enable_plane_detection' in ARCore config");
	}

	item = cJSON_GetObjectItemCaseSensitive(root, "enable_light_estimation");
	if (item != NULL && !android_parse_json_bool(item, &inout_cfg->enable_light_estimation)) {
		ANDROID_WARN(d, "Ignoring invalid 'enable_light_estimation' in ARCore config");
	}

	item = cJSON_GetObjectItemCaseSensitive(root, "enable_depth");
	if (item != NULL && !android_parse_json_bool(item, &inout_cfg->enable_depth)) {
		ANDROID_WARN(d, "Ignoring invalid 'enable_depth' in ARCore config");
	}

	item = cJSON_GetObjectItemCaseSensitive(root, "enable_instant_placement");
	if (item != NULL && !android_parse_json_bool(item, &inout_cfg->enable_instant_placement)) {
		ANDROID_WARN(d, "Ignoring invalid 'enable_instant_placement' in ARCore config");
	}

	item = cJSON_GetObjectItemCaseSensitive(root, "enable_augmented_faces");
	if (item != NULL && !android_parse_json_bool(item, &inout_cfg->enable_augmented_faces)) {
		ANDROID_WARN(d, "Ignoring invalid 'enable_augmented_faces' in ARCore config");
	}

	item = cJSON_GetObjectItemCaseSensitive(root, "enable_image_stabilization");
	if (item != NULL && !android_parse_json_bool(item, &inout_cfg->enable_image_stabilization)) {
		ANDROID_WARN(d, "Ignoring invalid 'enable_image_stabilization' in ARCore config");
	}
}

static bool
android_try_load_arcore_config_from_path(struct android_device *d, const char *path, struct arcore_min_config *inout_cfg)
{
	size_t file_size = 0;
	char *json_text = u_file_read_content_from_path(path, &file_size);
	if (json_text == NULL) {
		return false;
	}

	cJSON *root = cJSON_ParseWithLength(json_text, file_size);
	free(json_text);

	if (root == NULL) {
		ANDROID_WARN(d, "Failed to parse ARCore config JSON '%s'", path);
		return false;
	}

	if (!cJSON_IsObject(root)) {
		ANDROID_WARN(d, "ARCore config '%s' is not a JSON object", path);
		cJSON_Delete(root);
		return false;
	}

	android_apply_arcore_json_config(d, root, inout_cfg);
	cJSON_Delete(root);

	ANDROID_INFO(d, "Loaded ARCore config from '%s'", path);
	return true;
}

static void
android_load_arcore_config(struct android_device *d, struct arcore_min_config *inout_cfg)
{
	bool loaded = false;
	char create_path[PATH_MAX] = {0};

	const char *env_path = getenv(ARCORE_CONFIG_ENV_VAR);
	if (env_path != NULL && env_path[0] != '\0') {
		snprintf(create_path, sizeof(create_path), "%s", env_path);
		loaded = android_try_load_arcore_config_from_path(d, env_path, inout_cfg);
	}

	if (!loaded) {
		char files_dir[PATH_MAX] = {0};
		if (android_content_get_files_dir(android_globals_get_context(), files_dir, sizeof(files_dir))) {
			char json_path[PATH_MAX] = {0};
			snprintf(json_path, sizeof(json_path), "%s/%s", files_dir, ARCORE_CONFIG_JSON_FILENAME);

			if (create_path[0] == '\0') {
				snprintf(create_path, sizeof(create_path), "%s", json_path);
			}

			loaded = android_try_load_arcore_config_from_path(d, json_path, inout_cfg);
		}
	}

	if (!loaded) {
		const char *cwd_path = "./" ARCORE_CONFIG_JSON_FILENAME;
		if (create_path[0] == '\0') {
			snprintf(create_path, sizeof(create_path), "%s", cwd_path);
		}
		loaded = android_try_load_arcore_config_from_path(d, cwd_path, inout_cfg);
	}

	if (!loaded) {
		if (android_write_arcore_config_file(create_path, inout_cfg)) {
			ANDROID_INFO(d, "Created default ARCore config at '%s'", create_path);
		}
	}

	ANDROID_INFO(d,
	             "ARCore config: focus=%d camera_hz=%d texture_mode=%s plane=%d light=%d depth=%d instant=%d "
	             "faces=%d stabilization=%d",
	             (int)inout_cfg->focus_mode, (int)inout_cfg->camera_hz_mode,
	             android_arcore_texture_update_mode_to_string(inout_cfg->texture_update_mode),
	             (int)inout_cfg->enable_plane_detection, (int)inout_cfg->enable_light_estimation,
	             (int)inout_cfg->enable_depth, (int)inout_cfg->enable_instant_placement,
	             (int)inout_cfg->enable_augmented_faces, (int)inout_cfg->enable_image_stabilization);
}

static int32_t
android_clamp_sleep_usec(int32_t sleep_usec)
{
	if (sleep_usec < MIN_SLEEP_USEC) {
		return MIN_SLEEP_USEC;
	}
	if (sleep_usec > MAX_SLEEP_USEC) {
		return MAX_SLEEP_USEC;
	}

	return sleep_usec;
}

static inline float
android_clampf(float value, float min_value, float max_value)
{
	if (value < min_value) {
		return min_value;
	}
	if (value > max_value) {
		return max_value;
	}
	return value;
}

static inline float
android_vec3_length(const struct xrt_vec3 *v)
{
	return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
}

static inline struct xrt_vec3
android_gyro_from_sensor_event(const ASensorEvent *event)
{
	// Android phone coordinates to Monado device coordinates.
	return (struct xrt_vec3){.x = -event->data[1], .y = event->data[0], .z = event->data[2]};
}

static inline void
android_gyro_update_bias(struct android_gyro_stream *stream, const struct xrt_vec3 *raw)
{
	if (android_vec3_length(raw) > GYRO_BIAS_TRACK_MAX_RAD_S) {
		return;
	}

	if (!stream->have_bias) {
		stream->bias_device = *raw;
		stream->have_bias = true;
		return;
	}

	stream->bias_device.x += (raw->x - stream->bias_device.x) * GYRO_BIAS_TRACK_ALPHA;
	stream->bias_device.y += (raw->y - stream->bias_device.y) * GYRO_BIAS_TRACK_ALPHA;
	stream->bias_device.z += (raw->z - stream->bias_device.z) * GYRO_BIAS_TRACK_ALPHA;
}

static inline struct xrt_vec3
android_gyro_filter_sample(struct android_gyro_stream *stream, const struct xrt_vec3 *raw)
{
	android_gyro_update_bias(stream, raw);

	struct xrt_vec3 filtered = {
	    .x = raw->x - stream->bias_device.x,
	    .y = raw->y - stream->bias_device.y,
	    .z = raw->z - stream->bias_device.z,
	};

	const float magnitude = android_vec3_length(&filtered);
	if (magnitude > GYRO_SPIKE_CLAMP_RAD_S && magnitude > 0.0f) {
		const float scale = GYRO_SPIKE_CLAMP_RAD_S / magnitude;
		filtered.x *= scale;
		filtered.y *= scale;
		filtered.z *= scale;
	}

	return filtered;
}

static void
android_gyro_stream_stop(struct android_gyro_stream *stream)
{
	if (stream == NULL) {
		return;
	}

	if (stream->queue != NULL && stream->sensor != NULL) {
		ASensorEventQueue_disableSensor(stream->queue, stream->sensor);
	}
	if (stream->manager != NULL && stream->queue != NULL) {
		ASensorManager_destroyEventQueue(stream->manager, stream->queue);
	}

	memset(stream, 0, sizeof(*stream));
}

static bool
android_gyro_stream_start(struct android_device *d, struct android_gyro_stream *stream)
{
	memset(stream, 0, sizeof(*stream));

	stream->manager = ASensorManager_getInstance();
	if (stream->manager == NULL) {
		ANDROID_WARN(d, "Failed to get Android sensor manager");
		return false;
	}

#ifdef ASENSOR_TYPE_GYROSCOPE_UNCALIBRATED
	stream->sensor =
	    ASensorManager_getDefaultSensor(stream->manager, ASENSOR_TYPE_GYROSCOPE_UNCALIBRATED);
#endif
	if (stream->sensor == NULL) {
		stream->sensor = ASensorManager_getDefaultSensor(stream->manager, ASENSOR_TYPE_GYROSCOPE);
	}
	if (stream->sensor == NULL) {
		ANDROID_WARN(d, "No gyroscope sensor available");
		return false;
	}

	stream->looper = ALooper_forThread();
	if (stream->looper == NULL) {
		stream->looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
	}
	if (stream->looper == NULL) {
		ANDROID_WARN(d, "Failed to prepare looper for gyroscope events");
		return false;
	}

	stream->queue = ASensorManager_createEventQueue(stream->manager, stream->looper, LOOPER_ID_USER, NULL, NULL);
	if (stream->queue == NULL) {
		ANDROID_WARN(d, "Failed to create gyroscope event queue");
		return false;
	}

	if (ASensorEventQueue_enableSensor(stream->queue, stream->sensor) != 0) {
		ANDROID_WARN(d, "Failed to enable gyroscope sensor");
		android_gyro_stream_stop(stream);
		return false;
	}

	int32_t requested_usec = ASensor_getMinDelay(stream->sensor);
	requested_usec = android_clamp_sleep_usec(requested_usec > 0 ? requested_usec : POLL_RATE_USEC);
	if (ASensorEventQueue_setEventRate(stream->queue, stream->sensor, requested_usec) != 0) {
		ANDROID_WARN(d, "Failed to set gyroscope rate, using platform default");
	}

	return true;
}

static void
android_gyro_stream_poll_latest(struct android_gyro_stream *stream)
{
	if (stream == NULL || stream->queue == NULL) {
		return;
	}

	(void)ALooper_pollAll(0, NULL, NULL, NULL);

	ASensorEvent event;
	while (ASensorEventQueue_getEvents(stream->queue, &event, 1) > 0) {
		if (event.type != ASENSOR_TYPE_GYROSCOPE
#ifdef ASENSOR_TYPE_GYROSCOPE_UNCALIBRATED
		    && event.type != ASENSOR_TYPE_GYROSCOPE_UNCALIBRATED
#endif
		) {
			continue;
		}

		struct xrt_vec3 raw = android_gyro_from_sensor_event(&event);
		stream->latest.angular_velocity_device = android_gyro_filter_sample(stream, &raw);
		stream->latest.timestamp_ns = event.timestamp;
		stream->latest.valid = true;
	}
}

static void
android_gyro_stream_poll_noop(struct android_gyro_stream *stream)
{
	(void)stream;
}

static inline struct android_device *
android_device(struct xrt_device *xdev)
{
	return (struct android_device *)xdev;
}



static bool
read_file(pb_istream_t *stream, uint8_t *buf, size_t count)
{
	FILE *file = (FILE *)stream->state;
	if (buf == NULL) {
		while (count-- && fgetc(file) != EOF)
			;
		return count == 0;
	}

	bool status = (fread(buf, 1, count, file) == count);

	if (feof(file)) {
		stream->bytes_left = 0;
	}

	return status;
}

static bool
read_buffer(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	(void)field;

	struct decode_buffer *db = (struct decode_buffer *)*arg;
	if (db == NULL || db->data == NULL || db->capacity == 0) {
		return false;
	}

	const size_t max_used = db->capacity - (db->nul_terminate ? 1 : 0);
	if (db->used > max_used || stream->bytes_left > (max_used - db->used)) {
		return false;
	}

	const size_t read_count = stream->bytes_left;
	if (!pb_read(stream, db->data + db->used, read_count)) {
		return false;
	}

	db->used += read_count;
	if (db->nul_terminate) {
		db->data[db->used] = '\0';
	}

	return true;
}

static bool
load_cardboard_distortion(struct android_device *d,
                          struct xrt_android_display_metrics *metrics,
                          struct u_cardboard_distortion_arguments *args)
{
	(void)metrics;

	char external_storage_dir[PATH_MAX] = {0};
	if (!android_content_get_files_dir(android_globals_get_context(), external_storage_dir,
	                                   sizeof(external_storage_dir))) {
		ANDROID_ERROR(d, "failed to access files dir");
		return false;
	}

	/* TODO: put file in Cardboard folder */
	char device_params_file[PATH_MAX] = {0};
	snprintf(device_params_file, sizeof(device_params_file), "%s/current_device_params", external_storage_dir);

	FILE *file = fopen(device_params_file, "rb");
	if (file == NULL) {
		ANDROID_ERROR(d, "failed to open calibration file '%s'", device_params_file);
		return false;
	}

	pb_istream_t stream = {&read_file, file, SIZE_MAX, NULL};
	cardboard_DeviceParams params = cardboard_DeviceParams_init_zero;

	struct decode_buffer vendor_db = {0};
	char vendor[64] = {0};
	vendor_db.data = (uint8_t *)vendor;
	vendor_db.capacity = sizeof(vendor);
	vendor_db.nul_terminate = true;
	params.vendor.arg = &vendor_db;
	params.vendor.funcs.decode = read_buffer;

	struct decode_buffer model_db = {0};
	char model[64] = {0};
	model_db.data = (uint8_t *)model;
	model_db.capacity = sizeof(model);
	model_db.nul_terminate = true;
	params.model.arg = &model_db;
	params.model.funcs.decode = read_buffer;

	struct decode_buffer angles_db = {0};
	float angles[4] = {0};
	angles_db.data = (uint8_t *)angles;
	angles_db.capacity = sizeof(angles);
	params.left_eye_field_of_view_angles.arg = &angles_db;
	params.left_eye_field_of_view_angles.funcs.decode = read_buffer;

	struct decode_buffer distortion_db = {0};
	distortion_db.data = (uint8_t *)args->distortion_k;
	distortion_db.capacity = sizeof(args->distortion_k);
	params.distortion_coefficients.arg = &distortion_db;
	params.distortion_coefficients.funcs.decode = read_buffer;

	if (!pb_decode(&stream, cardboard_DeviceParams_fields, &params)) {
		ANDROID_ERROR(d, "failed to read calibration file: %s", PB_GET_ERROR(&stream));
		fclose(file);
		return false;
	}

	if (params.has_vertical_alignment) {
		args->vertical_alignment = (enum u_cardboard_vertical_alignment)params.vertical_alignment;
	}

	if (params.has_inter_lens_distance) {
		args->inter_lens_distance_meters = params.inter_lens_distance;
	}
	if (params.has_screen_to_lens_distance) {
		args->screen_to_lens_distance_meters = params.screen_to_lens_distance;
	}
	if (params.has_tray_to_lens_distance) {
		args->tray_to_lens_distance_meters = params.tray_to_lens_distance;
	}

	args->fov = (struct xrt_fov){.angle_left = -DEG_TO_RAD(angles[0]),
	                             .angle_right = DEG_TO_RAD(angles[1]),
	                             .angle_down = -DEG_TO_RAD(angles[2]),
	                             .angle_up = DEG_TO_RAD(angles[3])};

	ANDROID_INFO(d, "loaded calibration for device %s (%s)", model, vendor);

	fclose(file);
	return true;
}

static int32_t
android_get_initial_sleep_usec(const struct android_device *d)
{
	if (d == NULL) {
		return POLL_RATE_USEC;
	}

	const int64_t nominal_interval_ns = d->base.hmd->screens[0].nominal_frame_interval_ns;
	if (nominal_interval_ns <= 0) {
		return POLL_RATE_USEC;
	}

	const int32_t nominal_usec = (int32_t)(nominal_interval_ns / 1000);
	return android_clamp_sleep_usec(nominal_usec);
}

static float
android_get_nominal_frame_hz(const struct android_device *d)
{
	if (d == NULL) {
		return 60.0f;
	}

	const int64_t nominal_interval_ns = d->base.hmd->screens[0].nominal_frame_interval_ns;
	if (nominal_interval_ns <= 0) {
		return 60.0f;
	}

	const float nominal_hz = 1e9f / (float)nominal_interval_ns;
	return android_clampf(nominal_hz, ARCORE_SAMPLE_HZ_MIN, ARCORE_SAMPLE_HZ_MAX);
}

static float
android_update_arcore_sample_hz(float current_hz, int64_t frame_delta_ns)
{
	if (frame_delta_ns <= 0) {
		return current_hz;
	}

	const float measured_hz = android_clampf(1e9f / (float)frame_delta_ns, ARCORE_SAMPLE_HZ_MIN, ARCORE_SAMPLE_HZ_MAX);
	const float base_hz = current_hz > 0.0f ? current_hz : measured_hz;
	return base_hz + (measured_hz - base_hz) * ARCORE_SAMPLE_HZ_SMOOTHING;
}

static void
android_update_sleep_from_frame_delta(int32_t *sleep_usec,
                                      int64_t *last_frame_ts_ns,
                                      float *sample_hz,
                                      int64_t frame_ts_ns)
{
	if (*last_frame_ts_ns > 0 && frame_ts_ns > *last_frame_ts_ns) {
		const int64_t delta_ns = frame_ts_ns - *last_frame_ts_ns;
		const int32_t delta_usec = (int32_t)(delta_ns / 1000);
		*sleep_usec = android_clamp_sleep_usec(delta_usec);
		*sample_hz = android_update_arcore_sample_hz(*sample_hz, delta_ns);
	}

	*last_frame_ts_ns = frame_ts_ns;
}

static bool
android_push_arcore_relation(struct android_device *d,
                             const float position[3],
                             const float orientation[4],
                             int64_t frame_ts_ns,
                             float sample_hz,
                             const struct android_gyro_stream *gyro_stream)
{
	const int64_t now_mono_ns = os_monotonic_get_ns();
	const int64_t sample_ts_ns = m_clock_offset_a2b(
	    android_clampf(sample_hz, ARCORE_SAMPLE_HZ_MIN, ARCORE_SAMPLE_HZ_MAX), frame_ts_ns, now_mono_ns,
	    &d->arcore_ts_to_monotonic_ns);

	struct xrt_space_relation relation = XRT_SPACE_RELATION_ZERO;
	relation.pose.position = (struct xrt_vec3){position[0], position[1], position[2]};
	relation.pose.orientation = (struct xrt_quat){orientation[0], orientation[1], orientation[2], orientation[3]};
	relation.relation_flags = (enum xrt_space_relation_flags)(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	                                                          XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
	                                                          XRT_SPACE_RELATION_POSITION_VALID_BIT |
	                                                          XRT_SPACE_RELATION_POSITION_TRACKED_BIT);

	if (gyro_stream != NULL && gyro_stream->latest.valid) {
		math_quat_rotate_derivative(
		    &relation.pose.orientation, &gyro_stream->latest.angular_velocity_device, &relation.angular_velocity);
		relation.relation_flags = (enum xrt_space_relation_flags)(relation.relation_flags |
		                                                          XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT);
	}

	return m_relation_history_push(d->relation_hist, &relation, sample_ts_ns);
}

static void
android_process_arcore_tick(struct android_device *d,
                            const struct android_gyro_stream *gyro_stream,
                            int32_t *sleep_usec,
                            int64_t *last_frame_ts_ns,
                            float *sample_hz,
                            uint32_t *dropped_sample_count)
{
	float position[3] = {0};
	float orientation[4] = {0};
	bool tracking = false;
	int64_t frame_ts_ns = 0;

	if (!arcore_min_tick(&d->ar, position, orientation, &tracking, &frame_ts_ns)) {
		return;
	}
	if (!tracking || frame_ts_ns <= 0) {
		return;
	}

	android_update_sleep_from_frame_delta(sleep_usec, last_frame_ts_ns, sample_hz, frame_ts_ns);

	if (android_push_arcore_relation(d, position, orientation, frame_ts_ns, *sample_hz, gyro_stream)) {
		*dropped_sample_count = 0;
		return;
	}

	(*dropped_sample_count)++;
	if ((*dropped_sample_count % 120u) == 0u) {
		ANDROID_WARN(d, "Dropping ARCore samples due to non-monotonic timestamps (%u drops)",
		             *dropped_sample_count);
	}
}

static void
android_process_arcore_tick_noop(struct android_device *d,
                                 const struct android_gyro_stream *gyro_stream,
                                 int32_t *sleep_usec,
                                 int64_t *last_frame_ts_ns,
                                 float *sample_hz,
                                 uint32_t *dropped_sample_count)
{
	(void)d;
	(void)gyro_stream;
	(void)sleep_usec;
	(void)last_frame_ts_ns;
	(void)sample_hz;
	(void)dropped_sample_count;
}

static void
android_update_camera_passthrough_frame(struct android_device *d)
{
	bool enabled = false;

	os_mutex_lock(&d->camera_frame_mutex);
	enabled = d->camera_passthrough_enabled;
	os_mutex_unlock(&d->camera_frame_mutex);

	if (!enabled) {
		return;
	}

	struct xrt_passthrough_camera_frame new_frame = {
	    .handle = XRT_GRAPHICS_BUFFER_HANDLE_INVALID,
	};

	if (!arcore_min_get_latest_camera_frame(&d->ar, &new_frame.handle, &new_frame.width, &new_frame.height,
	                                        &new_frame.timestamp_ns) ||
	    !xrt_graphics_buffer_is_valid(new_frame.handle)) {
		return;
	}

	os_mutex_lock(&d->camera_frame_mutex);

	if (xrt_graphics_buffer_is_valid(d->camera_frame.handle)) {
		u_graphics_buffer_unref(&d->camera_frame.handle);
	}

	d->camera_frame = new_frame;

	os_mutex_unlock(&d->camera_frame_mutex);
}

static void *
android_run_thread(void *ptr)
{
	struct android_device *d = (struct android_device *)ptr;
	ANDROID_INFO(d, "ANDROID RUN THREAD START");
	int32_t sleep_usec = android_get_initial_sleep_usec(d);
	int64_t last_frame_ts_ns = 0;
	float arcore_sample_hz = android_get_nominal_frame_hz(d);
	uint32_t dropped_sample_count = 0;

	JavaVM *vm = (JavaVM *)android_globals_get_vm();
	jobject ctx = (jobject)android_globals_get_context();

	bool ar_ok = false;
	if (vm != NULL && ctx != NULL) {
		ar_ok = arcore_min_start_ex(&d->ar, vm, ctx, &d->ar_cfg);
	}
	if (!ar_ok) {
		ANDROID_WARN(d, "ARCore start failed (check runtime app has ARCore Java dependency)");
	}

	struct android_gyro_stream gyro_stream = {0};
	bool gyro_ok = android_gyro_stream_start(d, &gyro_stream);
	if (!gyro_ok) {
		ANDROID_WARN(d, "Gyroscope unavailable, reprojection will not include angular velocity");
	}

	void (*gyro_poll_fn)(struct android_gyro_stream *) =
	    gyro_ok ? android_gyro_stream_poll_latest : android_gyro_stream_poll_noop;
	const struct android_gyro_stream *gyro_for_tick = gyro_ok ? &gyro_stream : NULL;
	void (*process_tick_fn)(struct android_device *,
	                        const struct android_gyro_stream *,
	                        int32_t *,
	                        int64_t *,
	                        float *,
	                        uint32_t *) = ar_ok ? android_process_arcore_tick : android_process_arcore_tick_noop;

	struct timespec ts;
	os_thread_helper_lock(&d->oth);

	while (os_thread_helper_is_running_locked(&d->oth)) {
		os_thread_helper_unlock(&d->oth);

		gyro_poll_fn(&gyro_stream);
		process_tick_fn(d, gyro_for_tick, &sleep_usec, &last_frame_ts_ns, &arcore_sample_hz, &dropped_sample_count);
		if (ar_ok) {
			android_update_camera_passthrough_frame(d);
		}

		os_thread_helper_lock(&d->oth);

		clock_gettime(CLOCK_MONOTONIC, &ts);
		int64_t ns = (int64_t)ts.tv_nsec + (int64_t)sleep_usec * 1000;
		ts.tv_sec += ns / 1000000000LL;
		ts.tv_nsec = ns % 1000000000LL;

		(void)pthread_cond_timedwait(&d->oth.cond, &d->oth.mutex, &ts);
	}

	os_thread_helper_unlock(&d->oth);

	if (ar_ok) {
		arcore_min_stop(&d->ar);
	}
	android_gyro_stream_stop(&gyro_stream);

	ANDROID_INFO(d, "android_run_thread exit");
	return NULL;
}


/*
 *
 * Device functions.
 *
 */

static void
android_device_destroy(struct xrt_device *xdev)
{
	struct android_device *android = android_device(xdev);

	// Destroy the thread object.
	os_thread_helper_destroy(&android->oth);

	os_mutex_lock(&android->camera_frame_mutex);
	u_graphics_buffer_unref(&android->camera_frame.handle);
	os_mutex_unlock(&android->camera_frame_mutex);
	os_mutex_destroy(&android->camera_frame_mutex);

	m_relation_history_destroy(&android->relation_hist);

	// Remove the variable tracking.
	u_var_remove_root(android);

	free(android);
}

static xrt_result_t
android_device_get_tracked_pose(struct xrt_device *xdev,
                                enum xrt_input_name name,
                                int64_t at_timestamp_ns,
                                struct xrt_space_relation *out_relation)
{
	struct android_device *d = android_device(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		U_LOG_XDEV_UNSUPPORTED_INPUT(&d->base, d->log_level, name);
		return XRT_ERROR_INPUT_UNSUPPORTED;
	}

	struct xrt_space_relation new_relation = XRT_SPACE_RELATION_ZERO;
	enum m_relation_history_result history_result =
	    m_relation_history_get(d->relation_hist, at_timestamp_ns, &new_relation);
	if (history_result == M_RELATION_HISTORY_RESULT_INVALID) {
		new_relation = (struct xrt_space_relation)XRT_SPACE_RELATION_ZERO;
	}

	if ((new_relation.relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) != 0) {
		math_quat_normalize(&new_relation.pose.orientation);
	}

	*out_relation = new_relation;
	return XRT_SUCCESS;
}

static xrt_result_t
android_device_begin_feature(struct xrt_device *xdev, enum xrt_device_feature_type type)
{
	struct android_device *d = android_device(xdev);
	if (type != XRT_DEVICE_FEATURE_CAMERA_PASSTHROUGH) {
		return XRT_ERROR_FEATURE_NOT_SUPPORTED;
	}
	if (d->ar_cfg.texture_update_mode != ARCORE_MIN_TEXTURE_UPDATE_MODE_HARDWARE_BUFFER) {
		return XRT_ERROR_FEATURE_NOT_SUPPORTED;
	}

	os_mutex_lock(&d->camera_frame_mutex);
	d->camera_passthrough_enabled = true;
	os_mutex_unlock(&d->camera_frame_mutex);

	return XRT_SUCCESS;
}

static xrt_result_t
android_device_end_feature(struct xrt_device *xdev, enum xrt_device_feature_type type)
{
	struct android_device *d = android_device(xdev);
	if (type != XRT_DEVICE_FEATURE_CAMERA_PASSTHROUGH) {
		return XRT_ERROR_FEATURE_NOT_SUPPORTED;
	}
	if (d->ar_cfg.texture_update_mode != ARCORE_MIN_TEXTURE_UPDATE_MODE_HARDWARE_BUFFER) {
		return XRT_ERROR_FEATURE_NOT_SUPPORTED;
	}

	os_mutex_lock(&d->camera_frame_mutex);
	d->camera_passthrough_enabled = false;
	u_graphics_buffer_unref(&d->camera_frame.handle);
	d->camera_frame.width = 0;
	d->camera_frame.height = 0;
	d->camera_frame.timestamp_ns = 0;
	os_mutex_unlock(&d->camera_frame_mutex);

	return XRT_SUCCESS;
}

static xrt_result_t
android_device_get_passthrough_camera_frame(struct xrt_device *xdev, struct xrt_passthrough_camera_frame *out_frame)
{
	struct android_device *d = android_device(xdev);

	if (out_frame == NULL) {
		return XRT_ERROR_INVALID_ARGUMENT;
	}
	if (d->ar_cfg.texture_update_mode != ARCORE_MIN_TEXTURE_UPDATE_MODE_HARDWARE_BUFFER) {
		return XRT_ERROR_FEATURE_NOT_SUPPORTED;
	}

	*out_frame = (struct xrt_passthrough_camera_frame){
	    .handle = XRT_GRAPHICS_BUFFER_HANDLE_INVALID,
	};

	os_mutex_lock(&d->camera_frame_mutex);

	if (!xrt_graphics_buffer_is_valid(d->camera_frame.handle)) {
		os_mutex_unlock(&d->camera_frame_mutex);
		return XRT_ERROR_NO_IMAGE_AVAILABLE;
	}

	*out_frame = d->camera_frame;
	out_frame->handle = u_graphics_buffer_ref(d->camera_frame.handle);

	os_mutex_unlock(&d->camera_frame_mutex);

	if (!xrt_graphics_buffer_is_valid(out_frame->handle)) {
		*out_frame = (struct xrt_passthrough_camera_frame){
		    .handle = XRT_GRAPHICS_BUFFER_HANDLE_INVALID,
		};
		return XRT_ERROR_NO_IMAGE_AVAILABLE;
	}

	return XRT_SUCCESS;
}


/*
 *
 * Prober functions.
 *
 */

static xrt_result_t
android_device_compute_distortion(
    struct xrt_device *xdev, uint32_t view, float u, float v, struct xrt_uv_triplet *result)
{
	struct android_device *d = android_device(xdev);
	u_compute_distortion_cardboard(&d->cardboard.values[view], u, v, result);
	return XRT_SUCCESS;
}


struct android_device *
android_device_create(void)
{
	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	struct android_device *d = U_DEVICE_ALLOCATE(struct android_device, flags, 1, 0);

	d->base.name = XRT_DEVICE_GENERIC_HMD;
	u_device_populate_function_pointers(&d->base, android_device_get_tracked_pose, android_device_destroy);
	d->base.get_view_poses = u_device_get_view_poses;
	d->base.get_visibility_mask = u_device_get_visibility_mask;
	d->base.compute_distortion = android_device_compute_distortion;
	d->base.begin_feature = android_device_begin_feature;
	d->base.end_feature = android_device_end_feature;
	d->base.get_passthrough_camera_frame = android_device_get_passthrough_camera_frame;
	d->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;
	d->base.device_type = XRT_DEVICE_TYPE_HMD;
	snprintf(d->base.str, XRT_DEVICE_NAME_LEN, "Android Sensors");
	snprintf(d->base.serial, XRT_DEVICE_NAME_LEN, "Android Sensors");

	d->log_level = debug_get_log_option_android_log();
	os_mutex_init(&d->camera_frame_mutex);
	d->camera_frame = (struct xrt_passthrough_camera_frame){
	    .handle = XRT_GRAPHICS_BUFFER_HANDLE_INVALID,
	};
	d->camera_passthrough_enabled = false;

	m_relation_history_create(&d->relation_hist);
	if (d->relation_hist == NULL) {
		U_LOG_E("Failed to create relation history!");
		android_device_destroy(&d->base);
		return NULL;
	}
	d->arcore_ts_to_monotonic_ns = 0;
	arcore_min_config_set_defaults(&d->ar_cfg);
	android_load_arcore_config(d, &d->ar_cfg);

	struct xrt_android_display_metrics metrics = {0};
	if (!android_custom_surface_get_display_metrics(android_globals_get_vm(), android_globals_get_context(),
	                                                &metrics)) {
		U_LOG_E("Could not get Android display metrics.");
		/* Fallback to default values (Pixel 3) */
		metrics.width_pixels = 2960;
		metrics.height_pixels = 1440;
		metrics.density_dpi = 572;
		metrics.xdpi = 572.0f;
		metrics.ydpi = 572.0f;
		metrics.refresh_rate = 60.0f;
	}

	const float refresh_rate_hz = metrics.refresh_rate > 1.0f ? metrics.refresh_rate : 60.0f;
	const float xdpi = metrics.xdpi > 1.0f ? metrics.xdpi : (metrics.density_dpi > 0 ? (float)metrics.density_dpi : 572.0f);
	const float ydpi = metrics.ydpi > 1.0f ? metrics.ydpi : (metrics.density_dpi > 0 ? (float)metrics.density_dpi : 572.0f);

	d->base.hmd->screens[0].nominal_frame_interval_ns = time_s_to_ns(1.0f / refresh_rate_hz);

	const uint32_t w_pixels = metrics.width_pixels;
	const uint32_t h_pixels = metrics.height_pixels;

	const float angle = 45 * M_PI / 180.0; // 0.698132; // 40Deg in rads
	const float w_meters = ((float)w_pixels / xdpi) * 0.0254f;
	const float h_meters = ((float)h_pixels / ydpi) * 0.0254f;

	const struct u_cardboard_distortion_arguments cardboard_v1_distortion_args = {
	    .distortion_k = {0.441f, 0.156f, 0.f, 0.f, 0.f},
	    .screen =
	        {
	            .w_pixels = w_pixels,
	            .h_pixels = h_pixels,
	            .w_meters = w_meters,
	            .h_meters = h_meters,
	        },
	    .inter_lens_distance_meters = 0.06f,
	    .screen_to_lens_distance_meters = 0.042f,
	    .tray_to_lens_distance_meters = 0.035f,
	    .fov =
	        {
	            .angle_left = -angle,
	            .angle_right = angle,
	            .angle_up = angle,
	            .angle_down = -angle,
	        },
	    .vertical_alignment = U_CARDBOARD_VERTICAL_ALIGNMENT_BOTTOM,
	};
	struct u_cardboard_distortion_arguments args = cardboard_v1_distortion_args;
	if (!load_cardboard_distortion(d, &metrics, &args)) {
		ANDROID_WARN(
		    d, "Failed to load cardboard calibration file, falling back to Cardboard V1 distortion values");
		args = cardboard_v1_distortion_args;
	}

	u_distortion_cardboard_calculate(&args, d->base.hmd, &d->cardboard);

	// Android AR mode supports classic VR and camera compositing mode.
	size_t blend_mode_idx = 0;
	d->base.hmd->blend_modes[blend_mode_idx++] = XRT_BLEND_MODE_OPAQUE;
	if (d->ar_cfg.texture_update_mode == ARCORE_MIN_TEXTURE_UPDATE_MODE_HARDWARE_BUFFER) {
		d->base.hmd->blend_modes[blend_mode_idx++] = XRT_BLEND_MODE_ALPHA_BLEND;
	} else {
		ANDROID_INFO(d, "Disabling alpha blend advertisement: ARCore texture mode is not hardware buffer.");
	}
	d->base.hmd->blend_mode_count = blend_mode_idx;

	// Distortion information.
	u_distortion_mesh_fill_in_compute(&d->base);

	// Everything done, finally start the thread.
	os_thread_helper_init_monotonic(&d->oth);
	int ret = 0;

	ret = os_thread_helper_start(&d->oth, android_run_thread, d);
	if (ret != 0) {
		ANDROID_ERROR(d, "Failed to start thread!");
		android_device_destroy(&d->base);
		return NULL;
	}

	d->base.supported.orientation_tracking = true;
	d->base.supported.position_tracking = true;

	ANDROID_DEBUG(d, "Created device!");

	return d;
}
