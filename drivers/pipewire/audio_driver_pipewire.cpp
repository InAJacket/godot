/**************************************************************************/
/*  audio_driver_pipewire.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/
#include "audio_driver_pipewire.h"
#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/os/keyboard.h"
#include "core/version_generated.gen.h"
#include "pipewire/context.h"
#include "pipewire/core.h"
#include "pipewire/keys.h"
#include "pipewire/loop.h"
#include "pipewire/main-loop.h"
#include "pipewire/map.h"
#include "pipewire/node.h"
#include "pipewire/port.h"
#include "pipewire/properties.h"
#include "pipewire/stream.h"
#include "pipewire/thread-loop.h"
#include "servers/audio_server.h"
#include "spa/param/audio/raw.h"
#include "spa/utils/defs.h"
#include "spa/utils/list.h"
#include <spa/param/audio/format-utils.h>
#include <pipewire/impl.h>

#ifdef PULSEAUDIO_ENABLED

#include "core/config/project_settings.h"
#include "core/os/os.h"

void AudioDriverPipeWire::thread_func(void *data) {
	AudioDriverPipeWire *ad = static_cast<AudioDriverPipeWire *>(data);

	pw_main_loop_run(ad->main_loop);
}

void AudioDriverPipeWire::register_handler(void *data, uint32_t id,
                uint32_t permissions, const char *type, uint32_t version,
                const struct spa_dict *props)
{
	AudioDriverPipeWire *ad = static_cast<AudioDriverPipeWire *>(data);

	if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
		pw_node *node = (pw_node *) pw_registry_bind(ad->registry, id, type, PW_VERSION_NODE, 0);
		uint32_t err = pw_map_insert_new(&ad->node_map, node);

		if (err == SPA_ID_INVALID) {
			WARN_PRINT("Pipewire driver node_map error. AudioServer may not have accurate device list.");
		}
	}
}

void AudioDriverPipeWire::deregister_handler(void *data, uint32_t id) {
	AudioDriverPipeWire *ad = static_cast<AudioDriverPipeWire *>(data);
}

void AudioDriverPipeWire::on_process(void *data) {
	return;
	// The rest of this function is for outputting sound and doesn't work yet.
	// Returning early was easier than manually commenting out all those lines just for one commit.
	AudioDriverPipeWire *ad = static_cast<AudioDriverPipeWire *>(data);

	pw_buffer *b;
	spa_buffer *buf;
	int i, c, n_frames, stride;
	int16_t *dst;

	if ((b = pw_stream_dequeue_buffer(ad->out_stream)) == nullptr) {
        pw_log_warn("out of buffers: %m");
        return;
    }

	buf = b->buffer;
	if ((dst = (int16_t*) buf->datas[0].data) == nullptr) {
		return;
	}

	stride = sizeof(int16_t) * 2;

	n_frames = buf->datas[0].maxsize / stride;
	if (b->requested) {
		n_frames = SPA_MIN(b->requested, n_frames);
	}

	ad->samples_in.resize(n_frames * 2);

	ad->audio_server_process(n_frames, ad->samples_in.ptrw());

	for (i = 0; i < n_frames; i++) {
	for (c = 0; c < 2; c++) {
		dst[i] = (ad->samples_in[i] >> (16*c)) / 2;
	}
	}

	buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = stride;
    buf->datas[0].chunk->size = n_frames * stride;
		pw_stream_queue_buffer(ad->out_stream, b);
}

const struct pw_registry_events AudioDriverPipeWire::registry_events = {
        PW_VERSION_REGISTRY_EVENTS,
        .global = register_handler,
		.global_remove = deregister_handler,
};

const struct pw_stream_events AudioDriverPipeWire::stream_events = {
	PW_VERSION_STREAM_EVENTS,
	.process = on_process,
};

Error AudioDriverPipeWire::init() {
	bool ver_ok = false;
	pw_init(nullptr, nullptr);
	String version = pw_get_library_version();
	Vector<String> ver_parts = version.split(".");
	if (ver_parts.size() >= 2) {
		ver_ok = (ver_parts[0].to_int() >= 1); // libpipewire 1.0.0 or later
	}
	print_verbose(vformat("PipeWire %s detected.", version));
	if (!ver_ok) {
		print_verbose("Unsupported PipeWire library version!");
		return ERR_CANT_OPEN;
	}

	active.clear();
	exit_thread.clear();

	mix_rate = _get_configured_mix_rate();

	main_loop = pw_main_loop_new(nullptr);

	context = pw_context_new(pw_main_loop_get_loop(main_loop), nullptr, 0);

	core = pw_context_connect(context, nullptr, 0);

	registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
	node_map = PW_MAP_INIT(4);

	spa_zero(registry_listener);
	pw_registry_add_listener(registry, &registry_listener, &registry_events, this);

	//init_output_device();
	pw_properties * props = pw_properties_new(
			PW_KEY_MEDIA_TYPE, "Audio",
			PW_KEY_MEDIA_CATEGORY, "Playback",
			PW_KEY_MEDIA_ROLE, "Game",
			NULL);
	out_stream = pw_stream_new(core, "Godot-Audio-Output", props);

	const struct spa_pod *params[1];

	uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

	spa_audio_info_raw audio_info = SPA_AUDIO_INFO_RAW_INIT(
                                .format = SPA_AUDIO_FORMAT_S16_LE,
                                .rate = mix_rate,
								.channels = 2);

	params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &audio_info);

	pw_stream_add_listener(out_stream, &out_listener, &stream_events, this);

	pw_stream_connect(out_stream, SPA_DIRECTION_OUTPUT, PW_ID_ANY, PW_STREAM_FLAG_NONE, params, 1);

	thread.start(AudioDriverPipeWire::thread_func, this);

	return OK;
}

void AudioDriverPipeWire::start() {
	active.set();
}

int AudioDriverPipeWire::get_mix_rate() const {
	return mix_rate;
}

AudioDriver::SpeakerMode AudioDriverPipeWire::get_speaker_mode() const {
	return get_speaker_mode_by_total_channels(channels);
}

float AudioDriverPipeWire::get_latency() {
	lock();
	float latency = 0.0; //TODO
	unlock();
	return latency;
}

void AudioDriverPipeWire::lock() {
	mutex.lock();
}

void AudioDriverPipeWire::unlock() {
	mutex.unlock();
}

void AudioDriverPipeWire::finish() {
	if (!thread.is_started()) {
		return;
	}

	exit_thread.set();
	if (thread.is_started()) {
		thread.wait_to_finish();
	}

	finish_output_device();

	if (context) {
		pw_context_destroy(context);
		context = nullptr;
	}

	if (main_loop) {
		pw_main_loop_quit(main_loop);
		main_loop = nullptr;
	}
}

Error AudioDriverPipeWire::init_output_device() { //TODO NEXT
	return OK;
}

void AudioDriverPipeWire::finish_output_device() {} // WIP



PackedStringArray AudioDriverPipeWire::get_output_device_list() {
	return output_devices;
}

String AudioDriverPipeWire::get_output_device() {
	return output_device_name;
}

void AudioDriverPipeWire::set_output_device(const String &p_name) {
	lock();
	new_output_device = p_name;
	unlock();
}

Error AudioDriverPipeWire::input_start() {
	lock();
	unlock();

	return OK;
}

Error AudioDriverPipeWire::input_stop() {
	lock();
	pw_stream_destroy(in_stream);
	unlock();

	return OK;
}

PackedStringArray AudioDriverPipeWire::get_input_device_list() {
	return input_devices;
}

String AudioDriverPipeWire::get_input_device() {
	return input_device_name;
}

void AudioDriverPipeWire::set_input_device(const String &p_name) {
	lock();
	new_input_device = p_name;
	unlock();
}

AudioDriverPipeWire::AudioDriverPipeWire() {
	samples_in.clear();
	samples_out.clear();
}

AudioDriverPipeWire::~AudioDriverPipeWire() {}

#endif // PIPEWIRE_ENABLED
