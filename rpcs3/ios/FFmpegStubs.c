// FFmpeg is not available in the first iOS milestone.  Keep the media-facing
// HLE objects linkable, but make every entry point fail closed until an iOS
// FFmpeg package is added.

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/codec_desc.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/buffer.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

static int g_log_level = AV_LOG_QUIET;

static int unsupported(void)
{
	return AVERROR(ENOSYS);
}

AVBufferRef* av_buffer_create(uint8_t* data, size_t size,
	void (*free_callback)(void* opaque, uint8_t* data), void* opaque, int flags)
{
	(void)data; (void)size; (void)free_callback; (void)opaque; (void)flags;
	return NULL;
}

int av_channel_layout_copy(AVChannelLayout* dst, const AVChannelLayout* src)
{
	(void)dst; (void)src;
	return unsupported();
}

int av_channel_layout_describe(const AVChannelLayout* layout, char* buf, size_t size)
{
	(void)layout;
	if (buf && size) buf[0] = '\0';
	return unsupported();
}

const AVCodec* av_codec_iterate(void** opaque) { (void)opaque; return NULL; }
void av_dict_free(AVDictionary** dictionary) { if (dictionary) *dictionary = NULL; }
AVDictionaryEntry* av_dict_get(const AVDictionary* dictionary, const char* key,
	const AVDictionaryEntry* previous, int flags)
{
	(void)dictionary; (void)key; (void)previous; (void)flags;
	return NULL;
}
int av_dict_set(AVDictionary** dictionary, const char* key, const char* value, int flags)
{
	(void)dictionary; (void)key; (void)value; (void)flags;
	return unsupported();
}

void av_dump_format(AVFormatContext* context, int index, const char* url, int output)
{
	(void)context; (void)index; (void)url; (void)output;
}

AVFrame* av_frame_alloc(void) { return NULL; }
void av_frame_free(AVFrame** frame) { if (frame) *frame = NULL; }
int av_frame_get_buffer(AVFrame* frame, int align) { (void)frame; (void)align; return unsupported(); }
int av_frame_make_writable(AVFrame* frame) { (void)frame; return unsupported(); }
void av_frame_unref(AVFrame* frame) { (void)frame; }
void av_freep(void* pointer) { if (pointer) *(void**)pointer = NULL; }
int av_get_bytes_per_sample(enum AVSampleFormat format) { (void)format; return 0; }
const char* av_get_sample_fmt_name(enum AVSampleFormat format) { (void)format; return NULL; }
int av_image_fill_linesizes(int linesizes[4], enum AVPixelFormat format, int width)
{
	(void)linesizes; (void)format; (void)width;
	return unsupported();
}
int av_image_fill_pointers(uint8_t* data[4], enum AVPixelFormat format, int height,
	uint8_t* pointer, const int linesizes[4])
{
	(void)data; (void)format; (void)height; (void)pointer; (void)linesizes;
	return unsupported();
}
int av_image_get_buffer_size(enum AVPixelFormat format, int width, int height, int align)
{
	(void)format; (void)width; (void)height; (void)align;
	return unsupported();
}

int av_interleaved_write_frame(AVFormatContext* context, AVPacket* packet)
{
	(void)context; (void)packet;
	return unsupported();
}

int av_log_format_line2(void* pointer, int level, const char* format, va_list arguments,
	char* line, int line_size, int* print_prefix)
{
	(void)pointer; (void)level; (void)format; (void)arguments; (void)print_prefix;
	if (line && line_size > 0) line[0] = '\0';
	return 0;
}
int av_log_get_level(void) { return g_log_level; }
void av_log_set_callback(void (*callback)(void*, int, const char*, va_list)) { (void)callback; }
void av_log_set_level(int level) { g_log_level = level; }

const AVOutputFormat* av_muxer_iterate(void** opaque) { (void)opaque; return NULL; }
AVPacket* av_packet_alloc(void) { return NULL; }
void av_packet_free(AVPacket** packet) { if (packet) *packet = NULL; }
void av_packet_rescale_ts(AVPacket* packet, AVRational source, AVRational destination)
{
	(void)packet; (void)source; (void)destination;
}
void av_packet_unref(AVPacket* packet) { (void)packet; }
int av_read_frame(AVFormatContext* context, AVPacket* packet)
{
	(void)context; (void)packet;
	return unsupported();
}
int av_sample_fmt_is_planar(enum AVSampleFormat format) { (void)format; return 0; }
int av_samples_alloc(uint8_t** data, int* linesize, int channels, int samples,
	enum AVSampleFormat format, int align)
{
	(void)data; (void)linesize; (void)channels; (void)samples; (void)format; (void)align;
	return unsupported();
}
int av_strerror(int error_number, char* buffer, size_t size)
{
	(void)error_number;
	if (buffer && size) snprintf(buffer, size, "%s", "FFmpeg is unavailable on iOS");
	return 0;
}
int av_write_trailer(AVFormatContext* context) { (void)context; return unsupported(); }

AVCodecContext* avcodec_alloc_context3(const AVCodec* codec) { (void)codec; return NULL; }
const AVCodecDescriptor* avcodec_descriptor_get(enum AVCodecID id) { (void)id; return NULL; }
const AVCodec* avcodec_find_decoder(enum AVCodecID id) { (void)id; return NULL; }
const AVCodec* avcodec_find_encoder(enum AVCodecID id) { (void)id; return NULL; }
const AVCodec* avcodec_find_encoder_by_name(const char* name) { (void)name; return NULL; }
void avcodec_flush_buffers(AVCodecContext* context) { (void)context; }
void avcodec_free_context(AVCodecContext** context) { if (context) *context = NULL; }
int avcodec_get_supported_config(const AVCodecContext* context, const AVCodec* codec,
	enum AVCodecConfig config, unsigned flags, const void** output, int* count)
{
	(void)context; (void)codec; (void)config; (void)flags;
	if (output) *output = NULL;
	if (count) *count = 0;
	return unsupported();
}
int avcodec_open2(AVCodecContext* context, const AVCodec* codec, AVDictionary** options)
{
	(void)context; (void)codec; (void)options;
	return unsupported();
}
int avcodec_parameters_from_context(AVCodecParameters* parameters, const AVCodecContext* context)
{
	(void)parameters; (void)context;
	return unsupported();
}
int avcodec_receive_frame(AVCodecContext* context, AVFrame* frame)
{
	(void)context; (void)frame;
	return unsupported();
}
int avcodec_receive_packet(AVCodecContext* context, AVPacket* packet)
{
	(void)context; (void)packet;
	return unsupported();
}
int avcodec_send_frame(AVCodecContext* context, const AVFrame* frame)
{
	(void)context; (void)frame;
	return unsupported();
}
int avcodec_send_packet(AVCodecContext* context, const AVPacket* packet)
{
	(void)context; (void)packet;
	return unsupported();
}

AVFormatContext* avformat_alloc_context(void) { return NULL; }
int avformat_alloc_output_context2(AVFormatContext** context, const AVOutputFormat* format,
	const char* format_name, const char* filename)
{
	(void)format; (void)format_name; (void)filename;
	if (context) *context = NULL;
	return unsupported();
}
void avformat_close_input(AVFormatContext** context) { if (context) *context = NULL; }
int avformat_find_stream_info(AVFormatContext* context, AVDictionary** options)
{
	(void)context; (void)options;
	return unsupported();
}
void avformat_free_context(AVFormatContext* context) { (void)context; }
AVStream* avformat_new_stream(AVFormatContext* context, const AVCodec* codec)
{
	(void)context; (void)codec;
	return NULL;
}
int avformat_open_input(AVFormatContext** context, const char* url,
	const AVInputFormat* format, AVDictionary** options)
{
	(void)url; (void)format; (void)options;
	if (context) *context = NULL;
	return unsupported();
}
int avformat_query_codec(const AVOutputFormat* format, enum AVCodecID codec, int compliance)
{
	(void)format; (void)codec; (void)compliance;
	return 0;
}
int avformat_write_header(AVFormatContext* context, AVDictionary** options)
{
	(void)context; (void)options;
	return unsupported();
}
int avio_closep(AVIOContext** context) { if (context) *context = NULL; return 0; }
int avio_open(AVIOContext** context, const char* url, int flags)
{
	(void)url; (void)flags;
	if (context) *context = NULL;
	return unsupported();
}

int swr_alloc_set_opts2(SwrContext** context, const AVChannelLayout* output_layout,
	enum AVSampleFormat output_format, int output_rate, const AVChannelLayout* input_layout,
	enum AVSampleFormat input_format, int input_rate, int log_offset, void* log_context)
{
	(void)output_layout; (void)output_format; (void)output_rate; (void)input_layout;
	(void)input_format; (void)input_rate; (void)log_offset; (void)log_context;
	if (context) *context = NULL;
	return unsupported();
}
int swr_convert(SwrContext* context, uint8_t* const* output, int output_count,
	const uint8_t* const* input, int input_count)
{
	(void)context; (void)output; (void)output_count; (void)input; (void)input_count;
	return unsupported();
}
void swr_free(SwrContext** context) { if (context) *context = NULL; }
int swr_init(SwrContext* context) { (void)context; return unsupported(); }
int swr_is_initialized(SwrContext* context) { (void)context; return 0; }

void sws_freeContext(SwsContext* context) { (void)context; }
SwsContext* sws_getCachedContext(SwsContext* context, int source_width, int source_height,
	enum AVPixelFormat source_format, int destination_width, int destination_height,
	enum AVPixelFormat destination_format, int flags, SwsFilter* source_filter,
	SwsFilter* destination_filter, const double* parameters)
{
	(void)context; (void)source_width; (void)source_height; (void)source_format;
	(void)destination_width; (void)destination_height; (void)destination_format;
	(void)flags; (void)source_filter; (void)destination_filter; (void)parameters;
	return NULL;
}
SwsContext* sws_getContext(int source_width, int source_height, enum AVPixelFormat source_format,
	int destination_width, int destination_height, enum AVPixelFormat destination_format,
	int flags, SwsFilter* source_filter, SwsFilter* destination_filter, const double* parameters)
{
	(void)source_width; (void)source_height; (void)source_format;
	(void)destination_width; (void)destination_height; (void)destination_format;
	(void)flags; (void)source_filter; (void)destination_filter; (void)parameters;
	return NULL;
}
int sws_scale(SwsContext* context, const uint8_t* const source[], const int source_stride[],
	int source_y, int source_height, uint8_t* const destination[], const int destination_stride[])
{
	(void)context; (void)source; (void)source_stride; (void)source_y;
	(void)source_height; (void)destination; (void)destination_stride;
	return unsupported();
}
