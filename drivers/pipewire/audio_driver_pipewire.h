/**************************************************************************/
/*  audio_driver_pipewire.h                                               */
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

#ifndef AUDIO_DRIVER_PIPEWIRE_H
#define AUDIO_DRIVER_PIPEWIRE_H
#include "X11/Xdefs.h"
#include "core/templates/vector.h"
#include "pipewire/context.h"
#include "pipewire/core.h"
#include "pipewire/main-loop.h"
#include "pipewire/map.h"
#include "pipewire/node.h"
#include "pipewire/stream.h"
#include "spa/utils/hook.h"
#ifdef PIPEWIRE_ENABLED

#include "core/os/mutex.h"
#include "core/templates/safe_refcount.h"
#include "core/variant/variant.h"
#include "servers/audio_server.h"
#include <pipewire/pipewire.h>

class AudioDriverPipeWire : public AudioDriver {
	Mutex mutex;


	const void start_main_loop(pw_main_loop* loop);

	unsigned int mix_rate = 0;
	int channels = 2;

	SafeFlag active;
	SafeFlag exit_thread;

	pw_main_loop *main_loop = nullptr;
	pw_context *context = nullptr;
	pw_core *core = nullptr;
	pw_registry *registry = nullptr;
	pw_thread_loop *thread_loop = nullptr;
	spa_hook registry_listener;
	spa_hook out_listener;

	pw_stream *out_stream = nullptr;
	pw_stream *in_stream = nullptr;

	CharString name_char;

	String output_device_name = "Default";
	String new_output_device;
	PackedStringArray output_devices;

	String input_device_name;
	String new_input_device;
	PackedStringArray input_devices;

	Vector<int32_t> samples_in;
	Vector<int16_t> samples_out;

	Error init_output_device();
	void finish_output_device();

public:
	virtual const char *get_name() const override {
		return "PipeWire";
	};

	static const struct pw_registry_events registry_events;
	static const struct pw_stream_events stream_events;

	static void register_handler(void *data, uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props);
	static void on_process(void *data);

	virtual Error init() override;
	virtual void start() override;
	virtual int get_mix_rate() const override;
	virtual SpeakerMode get_speaker_mode() const override;
	virtual float get_latency() override;

	virtual void lock() override;
	virtual void unlock() override;
	virtual void finish() override;

	virtual PackedStringArray get_output_device_list() override;
	virtual String get_output_device() override;
	virtual void set_output_device(const String &p_name) override;

	virtual Error input_start() override;
	virtual Error input_stop() override;

	virtual PackedStringArray get_input_device_list() override;
	virtual String get_input_device() override;
	virtual void set_input_device(const String &p_name) override;

	AudioDriverPipeWire();
	~AudioDriverPipeWire();

};

#endif // PIPEWIRE_ENABLED

#endif // AUDIO_DRIVER_PULSEAUDIO
