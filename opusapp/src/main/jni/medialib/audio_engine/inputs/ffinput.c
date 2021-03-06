/*
 * ffinput.c
 *
 * Copyright (c) 2012, Philippe Chepy
 * All rights reserved.
 *
 * This software is the confidential and proprietary information
 * of Philippe Chepy.
 * You shall not disclose such Confidential Information.
 *
 * http://www.chepy.eu
 */

#include <unistd.h>
#include <assert.h>
#include <stdarg.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>

#include <audio_engine/inputs/ffinput.h>
#include <audio_engine/utils/memory.h>
#include <audio_engine/utils/log.h>

#define LOG_TAG "(jni).inputs.ffinput"

enum ffinput_media_file_error {
	MEDIA_FILE_OK = 0,
	MEDIA_FILE_ERROR_GENERIC = -1,
	MEDIA_FILE_ERROR_OPENNING = -2,
	MEDIA_FILE_ERROR_NO_STREAM_INFORMATIONS = -3,
	MEDIA_FILE_ERROR_NO_AUDIO_STREAM = -4,
	MEDIA_FILE_ERROR_OPENNING_DECODER = -5,
	MEDIA_FILE_ERROR_OPENNING_FILTER_BUFFER_SOURCE = -6,
	MEDIA_FILE_ERROR_OPENNING_FILTER_BUFFER_SINK = -7,
};

typedef struct {
	AVPacket packet;
	AVFrame * frame;
	char * audio_buffer;
} ffinput_input_context_s;

typedef struct {
	AVFormatContext * format_context;
	AVCodecContext * codec_context;
	AVFilterContext * buffer_sink_context;
	AVFilterContext * buffer_source_context;
	AVFilterGraph * filter_graph;
	int stream_index;
    engine_data_callback data_callback;
    engine_state_callback state_callback;
    void * user_context;

    int64_t seek_ts;
    int64_t seek_frame;

    ffinput_input_context_s * input_context;
    pthread_t decoding_thread;
    pthread_mutex_t validity_lock;
    int has_valid_thread;
} ffinput_stream_context_s;

int global_engine_initialized = 0;

/*
 * Prototype to timestamp when terminated stream.
 */
int ffinput_stream_set_position(engine_stream_context_s * stream, int64_t position);

static void * ffinput_decoding_thread(void * thread_arg) {
	engine_stream_context_s * stream = thread_arg;
	int got_frame;
	//AVFilterBufferRef *samplesref;
    AVFrame *filtered_frame = av_frame_alloc();
	int error_code = ENGINE_GENERIC_ERROR;
    ffinput_input_context_s * context = stream->engine->input->context;
	ffinput_stream_context_s * ffinput_stream = stream->stream_input_specific;

	for (;;) {
		error_code = ENGINE_GENERIC_ERROR;
		int	av_error_code = av_read_frame(ffinput_stream->format_context, &context->packet);

		if (av_error_code < 0) {
			if (av_error_code == AVERROR_EOF) {
				av_free_packet(&context->packet);
				error_code = ENGINE_TERMINATED;
				goto media_player_decode_frame_done;
			}

			char error_desc[1024];
			av_strerror(av_error_code, error_desc, sizeof(error_desc));
			LOG_ERROR(LOG_TAG, "cannot decode frame, stop (error_code = %i, %s)", av_error_code, error_desc);
			goto media_player_decode_frame_done;
		}

		if (context->packet.stream_index == ffinput_stream->stream_index) {
			av_error_code = avcodec_decode_audio4(ffinput_stream->codec_context, context->frame, &got_frame, &context->packet);

            // seeking.
            if (ffinput_stream->seek_ts >= 0) {
                int64_t ts = context->frame->pts == AV_NOPTS_VALUE ? context->packet.dts : context->packet.pts;
                if (ts == AV_NOPTS_VALUE) {
                    ts = 0;
                }

                if (ts >= ffinput_stream->seek_frame) {
                    ffinput_stream->seek_ts = -1;
                    ffinput_stream->seek_frame = -1;
                }
            }

            if (ffinput_stream->seek_ts < 0) {
                if (av_error_code < 0) {
                    LOG_WARNING(LOG_TAG, "cannot decode frame, skiping (error_code = %i)", av_error_code);
                    goto media_player_decode_frame_done;
                }

                if (got_frame) {
                    /* push the audio data from decoded frame into the filtergraph */
                    av_error_code = av_buffersrc_add_frame_flags(ffinput_stream->buffer_source_context, context->frame, 0);
                    if (av_error_code < 0) {
                        LOG_WARNING(LOG_TAG, "error while feeding the audio filtergraph.");
                    }
                    else {
                        /* pull filtered audio from the filtergraph */
                        while (1) {
                            av_error_code = av_buffersink_get_frame(ffinput_stream->buffer_sink_context, filtered_frame);

                            if(av_error_code == AVERROR(EAGAIN) || av_error_code == AVERROR_EOF) {
                                break;
                            }
                            if(av_error_code < 0) {
                                //return E_MEDIA_PLAYER_ERROR_FILTERING; /* E_ENGINE_ERROR_FILTERING -> goto end */
                                goto media_player_decode_frame_done;
                            }

                            size_t data_length = av_samples_get_buffer_size(NULL, av_get_channel_layout_nb_channels(av_frame_get_channel_layout(filtered_frame)), filtered_frame->nb_samples, AV_SAMPLE_FMT_S16, 1);
                            uint8_t * data = (uint8_t *) filtered_frame->data[0];

                            ffinput_stream->data_callback(stream, data, data_length);
                            av_frame_unref(filtered_frame);
                        }
                    }
                }
            }
		}

		av_free_packet(&context->packet);

		if (ffinput_stream->codec_context->codec->capabilities & CODEC_CAP_DELAY) {
			av_init_packet(&context->packet);
			// Decode all the remaining frames in the buffer, until the end is reached
			got_frame = 0;
			while (avcodec_decode_audio4(ffinput_stream->codec_context, context->frame, &got_frame, &context->packet) >= 0 && got_frame)
			{
			}
		}

		error_code = ENGINE_OK;

media_player_decode_frame_done:

		if (error_code < 0) {
			break;
		}

		if (error_code == ENGINE_TERMINATED /*|| error_code == ENGINE_GENERIC_ERROR*/) {
			ffinput_stream_set_position(stream, 0);
			ffinput_stream->state_callback(stream, STREAM_STATE_TERMINATED);
			break;
		}

		if (stream->decoder_is_stopping == 1) {
			break;
		}
	}

    av_frame_free(&filtered_frame);
	LOG_DEBUG(LOG_TAG, "ffinput_decoding_thread() : terminated. stream->decoder_is_stopping = %i", stream->decoder_is_stopping);
	stream->decoder_terminated = 1;
	return 0;
}

static int ffinput_init_filters(engine_stream_context_s * stream, const char * filter_desc) {
	ffinput_stream_context_s * ffinput_stream = stream->stream_input_specific;
	int error_code = ENGINE_GENERIC_ERROR;
	int av_error_code = 0;

	AVFilter * buffer_source = avfilter_get_by_name("abuffer");
	AVFilter * buffer_sink = avfilter_get_by_name("abuffersink");
	AVFilterInOut * outputs = avfilter_inout_alloc(); /* XXX */
	AVFilterInOut * inputs = avfilter_inout_alloc(); /* XXX */
	const enum AVSampleFormat out_sample_fmts[] = { AV_SAMPLE_FMT_S16, -1 };
//	enum AVSampleFormat out_sample_fmts[2] = { AV_SAMPLE_FMT_S16, -1 };
	static const int64_t out_channel_layouts[] = { AV_CH_LAYOUT_STEREO, -1 };
    static const int out_sample_rates[] = { 44100, -1 };
    const AVFilterLink * out_link;
	AVRational time_base = ffinput_stream->format_context->streams[ffinput_stream->stream_index]->time_base;

	char source_args[512];

	ffinput_stream->filter_graph = avfilter_graph_alloc(); /* XXX */

	if (ffinput_stream->codec_context->channel_layout == 0) {
		ffinput_stream->codec_context->channel_layout = av_get_default_channel_layout(ffinput_stream->codec_context->channels);
	}

	LOG_INFO(LOG_TAG, "stream->engine->param_channel_count = %i\n", (int)ffinput_stream->codec_context->channel_layout);
	LOG_INFO(LOG_TAG, "channel_layout = %"PRIx64"\n", ffinput_stream->codec_context->channel_layout);

	snprintf(source_args, sizeof(source_args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
			time_base.num, time_base.den, ffinput_stream->codec_context->sample_rate,
			av_get_sample_fmt_name(ffinput_stream->codec_context->sample_fmt),
			ffinput_stream->codec_context->channel_layout);

	av_error_code = avfilter_graph_create_filter(&ffinput_stream->buffer_source_context, buffer_source, "in", source_args, NULL, ffinput_stream->filter_graph);
	if (av_error_code < 0) {
		LOG_ERROR(LOG_TAG, "Cannot create audio buffer sink - in (error_code = %i)", av_error_code);
		goto media_file_init_filters_done;
	}

	av_error_code = avfilter_graph_create_filter(&ffinput_stream->buffer_sink_context, buffer_sink, "out", NULL, NULL, ffinput_stream->filter_graph);
	if (av_error_code < 0) {
	    LOG_ERROR(LOG_TAG, "Cannot create audio buffer sink - out (error_code = %i)", av_error_code);
		goto media_file_init_filters_done;
	}

    av_error_code = av_opt_set_int_list(ffinput_stream->buffer_sink_context, "sample_fmts", out_sample_fmts, -1, AV_OPT_SEARCH_CHILDREN);
    if (av_error_code < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
        goto media_file_init_filters_done;
    }

    av_error_code = av_opt_set_int_list(ffinput_stream->buffer_sink_context, "channel_layouts", out_channel_layouts, -1, AV_OPT_SEARCH_CHILDREN);
    if (av_error_code < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
        goto media_file_init_filters_done;
    }

    av_error_code = av_opt_set_int_list(ffinput_stream->buffer_sink_context, "sample_rates", out_sample_rates, -1, AV_OPT_SEARCH_CHILDREN);
    if (av_error_code < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output sample rate\n");
        goto media_file_init_filters_done;
    }

	outputs->name = av_strdup("in");
	outputs->filter_ctx = ffinput_stream->buffer_source_context;
	outputs->pad_idx = 0;
	outputs->next = NULL;

	inputs->name = av_strdup("out");
	inputs->filter_ctx = ffinput_stream->buffer_sink_context;
	inputs->pad_idx = 0;
	inputs->next = NULL;

	av_error_code = avfilter_graph_parse_ptr(ffinput_stream->filter_graph, filter_desc, &inputs, &outputs, NULL);
	if (av_error_code < 0) {
	    LOG_ERROR(LOG_TAG, "avfilter_graph_parse_ptr failure (error_code = %i)", av_error_code);
		goto media_file_init_filters_done;
	}

	av_error_code = avfilter_graph_config(ffinput_stream->filter_graph, NULL);
	if (av_error_code < 0) {
	    LOG_ERROR(LOG_TAG, "avfilter_graph_config failure (error_code = %i)", av_error_code);
		goto media_file_init_filters_done;
	}

     out_link = ffinput_stream->buffer_sink_context->inputs[0];
     av_get_channel_layout_string(source_args, sizeof(source_args), -1, out_link->channel_layout);
     LOG_INFO(LOG_TAG, "Output: srate:%dHz fmt:%s chlayout:%s",
             (int)out_link->sample_rate,
             (char *)av_x_if_null(av_get_sample_fmt_name(out_link->format), "?"),
             source_args);
	error_code = ENGINE_OK;
media_file_init_filters_done:

	return error_code;
}

static void av_callback(void *ptr, int level, const char *fmt, va_list vl) {
#if 0
    static char message[8192];
    const char *module = NULL;

    if (ptr)
    {
        AVClass *avc = *(AVClass**) ptr;
        module = avc->item_name(ptr);
    }

    vsnprintf(message, sizeof message, fmt, vl);

    LOG_INFO("(jni).libffmpeg", "@%s: %s", module, message);
#endif
}

int ffinput_new(engine_context_s * engine) {
	ffinput_input_context_s * context;
	int error_code = ENGINE_GENERIC_ERROR;

	engine->input->context = memory_zero_alloc(sizeof *context);
	if (engine->input->context == NULL) {
		LOG_INFO(LOG_TAG, "ffinput_new: memory allocation failure");
		error_code = ENGINE_ALLOC_ERROR;
		goto ffinput_new_done;
	}

    context = engine->input->context;

	av_log_set_callback(&av_callback);

	context->frame = av_frame_alloc();
  	if (context->frame == NULL) {
  		memory_free(engine->input->context);
  		error_code = ENGINE_ALLOC_ERROR;
		goto ffinput_new_done;
  	}

  	if (global_engine_initialized == 0) {
		avcodec_register_all();
		av_register_all();
		avfilter_register_all();
		global_engine_initialized = 1;
  	}

  	error_code = ENGINE_OK;

ffinput_new_done:
    return error_code;
}

int ffinput_delete(engine_context_s * engine) {
	ffinput_input_context_s * context;

	assert(engine);

	context = engine->input->context;

  	av_freep(&context->frame);
  	memory_free(context);
  	engine->input->context = NULL;

    return ENGINE_OK;
}

char * ffinput_get_name(engine_context_s * engine) {
	return "ffmpeg audio input";
}


int ffinput_get_max_channel_count(engine_context_s * engine, uint32_t * max_channels) {
	/*
		this file, in ffinput_stream_new() :
			if (stream->channel_count == 2) {
	*/
	*max_channels = 2;
	return ENGINE_OK;
}

int ffinput_stream_new(engine_context_s * engine, engine_stream_context_s * stream,
		const char * media_path, engine_data_callback data_callback, engine_state_callback state_callback, void * user_context) {
    int av_error_code = 0;
	int error_code = ENGINE_FILE_ACCESS_ERROR;
    AVCodec * codec;

	ffinput_stream_context_s * ffinput_stream = memory_zero_alloc(sizeof *ffinput_stream);
	if (ffinput_stream == NULL) {
		LOG_INFO(LOG_TAG, "audiotrack_new: memory allocation failure");
		goto ffinput_stream_new_done;
	}

	if (engine->sample_format == SAMPLE_FORMAT_FLOAT32_LE || engine->sample_format == SAMPLE_FORMAT_FLOAT32_BE) {
		error_code = ENGINE_INVALID_FORMAT_ERROR;
		goto ffinput_stream_new_done;
	}

	stream->stream_input_specific = ffinput_stream;

	av_error_code = avformat_open_input(&ffinput_stream->format_context, media_path, NULL, NULL);
    if (av_error_code < 0) {
		LOG_ERROR(LOG_TAG, "cannot open input file (path = %s, error code = %i)", media_path, av_error_code);
		goto ffinput_stream_new_done;
    }

    av_error_code = avformat_find_stream_info(ffinput_stream->format_context, NULL);
    if (av_error_code < 0) {
        LOG_ERROR(LOG_TAG, "cannot find stream information (path = %s, error_code = %i)", media_path, av_error_code);
		goto ffinput_stream_new_done;
    }

    av_error_code = av_find_best_stream(ffinput_stream->format_context, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (av_error_code < 0) {
        LOG_ERROR(LOG_TAG, "cannot find a audio stream in the input file (path = %s, error_code = %i)", media_path, av_error_code);
		goto ffinput_stream_new_done;
    }

    ffinput_stream->stream_index = av_error_code;
    ffinput_stream->codec_context = ffinput_stream->format_context->streams[ffinput_stream->stream_index]->codec;

    av_error_code = avcodec_open2(ffinput_stream->codec_context, codec, NULL);
    if (av_error_code < 0) {
		LOG_ERROR(LOG_TAG, "cannot open audio decoder (path = %s, error_code = %i)", media_path, av_error_code);
		goto ffinput_stream_new_done;
    }

    ffinput_stream->data_callback = data_callback;
    ffinput_stream->state_callback = state_callback;
    ffinput_stream->user_context = user_context;

    //LOG_ERROR(LOG_TAG, "audio decoder: set filters p");
    if (engine->channel_count == 2) {
    	error_code = ffinput_init_filters(stream, "aresample=44100:resampler=soxr:linear_interp=1");
    }
    else {
    	error_code = ffinput_init_filters(stream, "aresample=44100:resampler=soxr:linear_interp=1");
    }

    ffinput_stream->has_valid_thread = 0;
    pthread_mutex_init(&ffinput_stream->validity_lock, NULL);

    ffinput_stream->seek_ts = -1;
    ffinput_stream->seek_frame = -1;

ffinput_stream_new_done:
	if (error_code != ENGINE_OK) {
		if (ffinput_stream != NULL) {
			memory_free(ffinput_stream);
		}

		stream->stream_input_specific = NULL;
	}

	return error_code;
}

int ffinput_stream_delete(engine_stream_context_s * stream) {
	if (stream == NULL) {
		return ENGINE_OK;
	}

	ffinput_stream_context_s * ffinput_stream = stream->stream_input_specific;

	if (ffinput_stream != NULL) {
		pthread_mutex_destroy(&ffinput_stream->validity_lock);
		avfilter_graph_free(&ffinput_stream->filter_graph);

		if (ffinput_stream->codec_context) {
			avcodec_close(ffinput_stream->codec_context);
		}
		avformat_close_input(&ffinput_stream->format_context);

		memory_free(stream->stream_input_specific);
	}
    stream->stream_input_specific = NULL;

    return ENGINE_OK;
}

int ffinput_stream_start(engine_stream_context_s * stream) {
	ffinput_stream_context_s * ffinput_stream = stream->stream_input_specific;
	int error_code = ENGINE_GENERIC_ERROR;

	pthread_mutex_lock(&ffinput_stream->validity_lock);
	ffinput_stream->has_valid_thread = 1;
	ffinput_stream->state_callback(stream, STREAM_STATE_STARTED);
	stream->decoder_is_stopping = 0;
	stream->decoder_terminated = 0;

    if (pthread_create(&ffinput_stream->decoding_thread, NULL, ffinput_decoding_thread, stream) == 0) {
    	error_code = ENGINE_OK;
    }
    pthread_mutex_unlock(&ffinput_stream->validity_lock);

    return error_code;
}

int ffinput_stream_stop(engine_stream_context_s * stream) {
	ffinput_stream_context_s * ffinput_stream = stream->stream_input_specific;
	int error_code = ENGINE_GENERIC_ERROR;

	LOG_DEBUG(LOG_TAG, "ffinput_stream_stop()");

	pthread_mutex_lock(&ffinput_stream->validity_lock);
	if (ffinput_stream->has_valid_thread) {
		ffinput_stream->has_valid_thread = 0;
		stream->decoder_is_stopping = 1;
		/* sync */ pthread_cond_broadcast(&stream->buffer_full_cond);
		if (pthread_join(ffinput_stream->decoding_thread, NULL) == 0) {
			error_code = ENGINE_OK;
		}
		ffinput_stream->state_callback(stream, STREAM_STATE_STOPPED);
		LOG_DEBUG(LOG_TAG, "ffinput_stream_stop() : stopped");
	}
	else {
		LOG_DEBUG(LOG_TAG, "ffinput_stream_stop() : already stopped");
	}
	pthread_mutex_unlock(&ffinput_stream->validity_lock);

	return error_code;
}

int ffinput_stream_get_duration(engine_stream_context_s * stream, int64_t * duration) {
	ffinput_stream_context_s * ffinput_stream = stream->stream_input_specific;

	if (duration == NULL) {
		return ENGINE_GENERIC_ERROR;
	}

	int64_t max_frame_id = av_rescale(ffinput_stream->format_context->streams[ffinput_stream->stream_index]->duration,
				ffinput_stream->format_context->streams[ffinput_stream->stream_index]->time_base.num * 1000,
				ffinput_stream->format_context->streams[ffinput_stream->stream_index]->time_base.den);
	*duration = max_frame_id;

	return ENGINE_OK;
}

int ffinput_stream_set_position(engine_stream_context_s * stream, int64_t position) {
	ffinput_stream_context_s * ffinput_stream = stream->stream_input_specific;

    int64_t frame_id = av_rescale(position,
        ffinput_stream->format_context->streams[ffinput_stream->stream_index]->time_base.den,
        ffinput_stream->format_context->streams[ffinput_stream->stream_index]->time_base.num);
    frame_id = frame_id / 1000;

    // inaccurate seeking, improved by decoding without output...
    av_seek_frame(
        ffinput_stream->format_context,
        ffinput_stream->stream_index,
        frame_id,
        AVSEEK_FLAG_ANY);

	ffinput_stream->seek_ts = position;
	ffinput_stream->seek_frame = frame_id;
	avcodec_flush_buffers(ffinput_stream->format_context->streams[ffinput_stream->stream_index]->codec);
	return ENGINE_OK;
}

static engine_input_s ffinput_input = {
	.create = ffinput_new,
	.destroy = ffinput_delete,
	.get_name = ffinput_get_name,
	.get_max_channel_count = ffinput_get_max_channel_count,
	.stream_create = ffinput_stream_new,
	.stream_destroy = ffinput_stream_delete,
	.stream_start = ffinput_stream_start,
	.stream_stop = ffinput_stream_stop,
	.stream_get_duration = ffinput_stream_get_duration,
	.stream_set_position = ffinput_stream_set_position
};

engine_input_s * ffinput_get_input() {
	return &ffinput_input;
}
