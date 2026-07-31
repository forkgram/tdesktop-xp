// XP walk harness: reproduce media_audio_capture.cpp's encoder setup against the
// rebuilt Libraries-walk FFmpeg, to prove voice-message recording can work.
//
//   opus muxer -> avcodec_find_encoder(fmt->audio_codec) -> avcodec_open2
//   -> encode a second of a sine tone -> write the ogg/opus stream to memory.
//
// Build (from the v141_xp env, same libs Telegram links):
//   cl /MT /EHsc /I<ffmpeg> ffopus_xp.cpp <ffmpeg .a libs> opus.lib fpcompat.lib ...
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <cstdio>
#include <cmath>
#include <vector>
#include <cstring>

namespace {

std::vector<unsigned char> Result;
long long Offset = 0;

int WriteData(void *opaque, unsigned char *buf, int size) {
	if (Offset + size > (long long)Result.size()) {
		Result.resize(size_t(Offset + size));
	}
	memcpy(Result.data() + Offset, buf, size);
	Offset += size;
	return size;
}

int64_t SeekData(void *opaque, int64_t offset, int whence) {
	if (whence == SEEK_SET) Offset = offset;
	else if (whence == SEEK_CUR) Offset += offset;
	else if (whence == SEEK_END) Offset = (long long)Result.size() + offset;
	else if (whence == AVSEEK_SIZE) return (int64_t)Result.size();
	else return -1;
	return Offset;
}

const char *FormatName(AVSampleFormat f) {
	const auto name = av_get_sample_fmt_name(f);
	return name ? name : "?";
}

} // namespace

int main() {
#if LIBAVFORMAT_VERSION_MAJOR < 58
	av_register_all();
	avcodec_register_all();
	printf("av_register_all() called (lavf major %d)\n", LIBAVFORMAT_VERSION_MAJOR);
#endif

	avfilter_register_all();

	// Inventory check for the widened build.
	auto missing = 0;
	const char *decoders[] = { "ac3", "eac3", "dca", "alac", "ape", "vc1",
		"wmv3", "mjpeg", "flv", "mpeg1video", "theora", "amrnb", "adpcm_ms",
		"h264", "hevc", "vp8", "vp9", "opus", "aac", nullptr };
	printf("decoders:");
	for (auto i = 0; decoders[i]; ++i) {
		const auto found = avcodec_find_decoder_by_name(decoders[i]);
		printf(" %s=%s", decoders[i], found ? "ok" : "MISSING");
		if (!found) ++missing;
	}
	printf("\n");
	const char *demuxers[] = { "avi", "asf", "flv", "mpegts", "mpeg", "rm",
		"matroska,webm", "mov,mp4,m4a,3gp,3g2,mj2", "ogg", "wav", "mp3", nullptr };
	printf("demuxers:");
	for (auto i = 0; demuxers[i]; ++i) {
		const auto found = av_find_input_format(demuxers[i]);
		printf(" %s=%s", demuxers[i], found ? "ok" : "MISSING");
		if (!found) ++missing;
	}
	printf("\n");
	const char *filters[] = { "abuffer", "abuffersink", "atempo", nullptr };
	printf("filters:");
	for (auto i = 0; filters[i]; ++i) {
		const auto found = avfilter_get_by_name(filters[i]);
		printf(" %s=%s", filters[i], found ? "ok" : "MISSING");
		if (!found) ++missing;
	}
	printf("\n");
	const char *encoders[] = { "libopus", "aac", "libopenh264", "pcm_s16le", nullptr };
	printf("encoders:");
	for (auto i = 0; encoders[i]; ++i) {
		const auto found = avcodec_find_encoder_by_name(encoders[i]);
		printf(" %s=%s", encoders[i], found ? "ok" : "MISSING");
		if (!found) ++missing;
	}
	printf("\n");
	printf("mp4 muxer=%s\n", av_guess_format("mp4", nullptr, nullptr) ? "ok" : "MISSING");
	printf("== inventory missing: %d ==\n", missing);

	AVOutputFormat *fmt = nullptr;
	while ((fmt = av_oformat_next(fmt))) {
		if (!strcmp(fmt->name, "opus")) break;
	}
	if (!fmt) { printf("FAIL: no opus muxer\n"); return 1; }
	printf("opus muxer found, audio_codec=%d\n", (int)fmt->audio_codec);

	printf("registered encoders:");
	{
		AVCodec *it = nullptr;
		int n = 0;
		while ((it = av_codec_next(it))) {
			if (av_codec_is_encoder(it)) { printf(" %s(id=%d)", it->name, (int)it->id); ++n; }
		}
		printf("  [encoders %d]\n", n);
		it = nullptr;
		n = 0;
		while ((it = av_codec_next(it))) {
			if (av_codec_is_decoder(it)) ++n;
		}
		printf("registered decoders: %d\n", n);
	}
	printf("by_name(libopus)=%p by_name(opus)=%p\n",
		(void*)avcodec_find_encoder_by_name("libopus"),
		(void*)avcodec_find_encoder_by_name("opus"));

	const auto codec = avcodec_find_encoder(fmt->audio_codec);
	if (!codec) { printf("FAIL: avcodec_find_encoder(opus) -> NULL\n"); return 1; }
	printf("encoder found: %s (%s)\n", codec->name, codec->long_name ? codec->long_name : "");

	printf("supported sample formats:");
	for (int i = 0; codec->sample_fmts && codec->sample_fmts[i] != AV_SAMPLE_FMT_NONE; ++i) {
		printf(" %s", FormatName(codec->sample_fmts[i]));
	}
	printf("\n");

	AVFormatContext *fc = nullptr;
	auto buffer = (unsigned char*)av_malloc(4096);
	auto io = avio_alloc_context(buffer, 4096, 1, nullptr, nullptr, &WriteData, &SeekData);
	if (avformat_alloc_output_context2(&fc, fmt, nullptr, nullptr) < 0) {
		printf("FAIL: avformat_alloc_output_context2\n"); return 1;
	}
	fc->pb = io;
	fc->flags |= AVFMT_FLAG_CUSTOM_IO;

	auto stream = avformat_new_stream(fc, codec);
	auto cc = avcodec_alloc_context3(codec);

	// Same choice media_audio_capture.cpp now makes: prefer FLTP, else the
	// encoder's first supported format.
	auto sampleFmt = AV_SAMPLE_FMT_FLTP;
	if (codec->sample_fmts) {
		bool supported = false;
		auto first = AV_SAMPLE_FMT_NONE;
		for (int i = 0; codec->sample_fmts[i] != AV_SAMPLE_FMT_NONE; ++i) {
			if (first == AV_SAMPLE_FMT_NONE) first = codec->sample_fmts[i];
			if (codec->sample_fmts[i] == AV_SAMPLE_FMT_FLTP) { supported = true; break; }
		}
		if (!supported && first != AV_SAMPLE_FMT_NONE) sampleFmt = first;
	}
	printf("using sample_fmt=%s\n", FormatName(sampleFmt));

	cc->sample_fmt = sampleFmt;
	cc->bit_rate = 32000;
	cc->channel_layout = AV_CH_LAYOUT_MONO;
	cc->channels = 1;
	cc->sample_rate = 48000;
	if (fc->oformat->flags & AVFMT_GLOBALHEADER) {
		cc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}

	char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
	auto res = avcodec_open2(cc, codec, nullptr);
	if (res < 0) {
		printf("FAIL: avcodec_open2 -> %d (%s)\n", res,
			av_make_error_string(err, sizeof(err), res));
		return 1;
	}
	printf("avcodec_open2 OK, frame_size=%d\n", cc->frame_size);

	if (avcodec_parameters_from_context(stream->codecpar, cc) < 0) {
		printf("FAIL: avcodec_parameters_from_context\n"); return 1;
	}
	res = avformat_write_header(fc, nullptr);
	if (res < 0) {
		printf("FAIL: avformat_write_header -> %d (%s)\n", res,
			av_make_error_string(err, sizeof(err), res));
		return 1;
	}
	printf("avformat_write_header OK\n");

	// One second of a 440 Hz tone, encoded frame by frame.
	const auto frameSize = cc->frame_size ? cc->frame_size : 960;
	auto frame = av_frame_alloc();
	frame->format = cc->sample_fmt;
	frame->channel_layout = cc->channel_layout;
	frame->channels = 1;
	frame->sample_rate = cc->sample_rate;
	frame->nb_samples = frameSize;
	if (av_frame_get_buffer(frame, 0) < 0) { printf("FAIL: av_frame_get_buffer\n"); return 1; }

	auto packets = 0, bytes = 0;
	double phase = 0.;
	for (auto i = 0; i < 48000 / frameSize; ++i) {
		av_frame_make_writable(frame);
		for (auto s = 0; s < frameSize; ++s, phase += 2. * 3.14159265 * 440. / 48000.) {
			const auto value = sin(phase);
			if (cc->sample_fmt == AV_SAMPLE_FMT_S16) {
				((short*)frame->data[0])[s] = short(value * 20000);
			} else {
				((float*)frame->data[0])[s] = float(value * 0.6);
			}
		}
		frame->pts = int64_t(i) * frameSize;
		res = avcodec_send_frame(cc, frame);
		if (res < 0) {
			printf("FAIL: avcodec_send_frame -> %d (%s)\n", res,
				av_make_error_string(err, sizeof(err), res));
			return 1;
		}
		AVPacket pkt = { 0 };
		av_init_packet(&pkt);
		while ((res = avcodec_receive_packet(cc, &pkt)) == 0) {
			pkt.stream_index = stream->index;
			bytes += pkt.size;
			++packets;
			av_interleaved_write_frame(fc, &pkt);
			av_packet_unref(&pkt);
		}
		if (res != AVERROR(EAGAIN) && res != AVERROR_EOF) {
			printf("FAIL: avcodec_receive_packet -> %d (%s)\n", res,
				av_make_error_string(err, sizeof(err), res));
			return 1;
		}
	}
	av_write_trailer(fc);
	printf("encoded packets=%d payload=%d bytes, ogg/opus stream=%d bytes\n",
		packets, bytes, (int)Result.size());

	if (Result.size() > 4 && !memcmp(Result.data(), "OggS", 4)) {
		printf("PASS: output starts with OggS\n");
	} else {
		printf("FAIL: output does not start with OggS\n");
		return 1;
	}

	FILE *f = fopen("C:/TBuild/xp-port/_ffopus_test.ogg", "wb");
	if (f) { fwrite(Result.data(), 1, Result.size(), f); fclose(f); }
	printf("wrote C:/TBuild/xp-port/_ffopus_test.ogg\n");
	return 0;
}
