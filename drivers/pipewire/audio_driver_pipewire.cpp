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
#include "core/version_generated.gen.h"
#include "pipewire/context.h"
#include "pipewire/core.h"
#include "pipewire/loop.h"
#include "pipewire/main-loop.h"
#include "spa/utils/defs.h"
#include "spa/utils/list.h"
#include <cstddef>

#ifdef PULSEAUDIO_ENABLED

#include "core/config/project_settings.h"
# include "core/os/os.h"

void AudioDriverPipeWire::thread_func(void *p_udata) {
	AudioDriverPipeWire *driver = static_cast<AudioDriverPipeWire *>(p_udata);

	driver->start_main_loop(driver->main_loop);
}

void AudioDriverPipeWire::registry_event_global(void *data, uint32_t id,
                uint32_t permissions, const char *type, uint32_t version,
                const struct spa_dict *props)
{
        printf("object: id:%u type:%s/%d\n", id, type, version);
}

const struct pw_registry_events AudioDriverPipeWire::registry_events = {
        PW_VERSION_REGISTRY_EVENTS,
        .global = registry_event_global,
};

const void AudioDriverPipeWire::start_main_loop(pw_main_loop* loop) {
	pw_main_loop_run(loop);
}

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

	spa_zero(registry_listener);
	pw_registry_add_listener(registry, &registry_listener, &registry_events, nullptr);

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
	return 0.0;
	// WIP
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
		pw_main_loop_quit(main_loop); // Might not be the right function.
		main_loop = nullptr;
	}
}

Error AudioDriverPipeWire::init_output_device() { // WIP
	return OK;
}

void AudioDriverPipeWire::finish_output_device() {} // WIP

Error AudioDriverPipeWire::init_input_device() { // WIP
	return OK;
}

void AudioDriverPipeWire::finish_input_device() {} // WIP

PackedStringArray AudioDriverPipeWire::get_output_device_list() {
	pw_out_devices.clear();
	pw_out_devices.push_back("Default");

	if (context == nullptr) {
		return pw_out_devices;
	}

	return pw_out_devices; // WIP
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
	Error err = init_input_device();
	unlock();

	return err;
}

Error AudioDriverPipeWire::input_stop() {
	lock();
	finish_input_device();
	unlock();

	return OK;
}

PackedStringArray AudioDriverPipeWire::get_input_device_list() {
	PackedStringArray a;
	return a;
}

String AudioDriverPipeWire::get_input_device() {
	return input_device_name;
}

void AudioDriverPipeWire::set_input_device(const String &p_name) {
	lock();
	new_input_device = p_name;
	unlock();
}

AudioDriverPipeWire::AudioDriverPipeWire() {}

AudioDriverPipeWire::~AudioDriverPipeWire() {}

#endif // PIPEWIRE_ENABLED
