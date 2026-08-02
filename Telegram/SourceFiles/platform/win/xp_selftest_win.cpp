/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/xp_selftest_win.h"

#include "ffmpeg/ffmpeg_utility.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_frame_generator.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtGui/QImage>

#include <al.h>
#include <alc.h>

#include <windows.h>
#include <atomic>
#include <cstdio>
#include <thread>

extern "C" {
#include <libavfilter/avfilter.h>
} // extern "C"

namespace Platform::XpSelfTest {
namespace {

// Long enough that a 1-vCPU XP guest under load still finishes, short enough
// that a deadlock (the rlottie scheduler one, for instance) is reported as a
// hang instead of hanging the job until the CI runner's own timeout.
constexpr auto kProbeTimeoutMs = crl::time(60 * 1000);

enum class Result {
	Ok,
	Skip,
	Fail,
};

struct Report {
	QStringList lines;
	int ok = 0;
	int skip = 0;
	int fail = 0;
};

std::atomic<crl::time> GlobalProbeDeadline/* = 0*/;
std::atomic<bool> GlobalFinished/* = false*/;
QString GlobalStage;
QString GlobalReportPath;

void WriteLine(const QString &line) {
	const auto utf8 = line.toUtf8();
	std::fwrite(utf8.constData(), 1, utf8.size(), stdout);
	std::fwrite("\n", 1, 1, stdout);
	std::fflush(stdout);
	if (!GlobalReportPath.isEmpty()) {
		auto file = QFile(GlobalReportPath);
		if (file.open(QIODevice::Append | QIODevice::WriteOnly)) {
			file.write(utf8);
			file.write("\n");
		}
	}
}

// A probe that never returns is the failure mode this whole harness exists for:
// the chat-open deadlock showed up as a frozen window, which no exit code and no
// screenshot could describe. Kill the process instead, with the stage named.
void StartWatchdog() {
	std::thread([] {
		while (!GlobalFinished.load()) {
			const auto deadline = GlobalProbeDeadline.load();
			if (deadline && crl::now() > deadline) {
				WriteLine(u"XPSELFTEST stage=%1 status=HANG detail=no result in %2ms"_q
					.arg(GlobalStage)
					.arg(kProbeTimeoutMs));
				WriteLine(u"XPSELFTEST RESULT code=3 (watchdog)"_q);
				TerminateProcess(GetCurrentProcess(), 3);
			}
			Sleep(200);
		}
	}).detach();
}

[[nodiscard]] Result ProbeFfmpegRegistry(QString &detail) {
	// The single most expensive bug of this port: FFmpeg 3.4 registers nothing on
	// its own, so every container failed with INVALIDDATA until EnsureRegistered()
	// called av_register_all(). The trimmed build ALSO lost matroska/vp8/vp9 once,
	// which looked identical from the outside. Check both here.
	FFmpeg::EnsureRegistered();

	auto missing = QStringList();
	const auto demuxers = { "matroska", "mov", "mp3", "ogg", "wav" };
	for (const auto name : demuxers) {
		if (!av_find_input_format(name)) {
			missing.push_back(u"demuxer:%1"_q.arg(name));
		}
	}
	const auto decoders = {
		std::pair{ AV_CODEC_ID_VP8, "vp8" },
		std::pair{ AV_CODEC_ID_VP9, "vp9" },
		std::pair{ AV_CODEC_ID_H264, "h264" },
		std::pair{ AV_CODEC_ID_OPUS, "opus" },
		std::pair{ AV_CODEC_ID_AAC, "aac" },
		std::pair{ AV_CODEC_ID_MP3, "mp3" },
		std::pair{ AV_CODEC_ID_GIF, "gif" },
	};
	for (const auto &[id, name] : decoders) {
		if (!avcodec_find_decoder(id)) {
			missing.push_back(u"decoder:%1"_q.arg(name));
		}
	}
	if (!avcodec_find_encoder_by_name("libopus")) {
		missing.push_back(u"encoder:libopus"_q);
	}
	// Playback speed silently disappears without these three.
	const auto filters = { "atempo", "abuffer", "abuffersink", "aresample" };
	for (const auto name : filters) {
		if (!avfilter_get_by_name(name)) {
			missing.push_back(u"filter:%1"_q.arg(name));
		}
	}
	if (!missing.isEmpty()) {
		detail = u"missing "_q + missing.join(',');
		return Result::Fail;
	}
	detail = u"avformat %1 avcodec %2 avfilter %3"_q
		.arg(LIBAVFORMAT_VERSION_MAJOR)
		.arg(LIBAVCODEC_VERSION_MAJOR)
		.arg(LIBAVFILTER_VERSION_MAJOR);
	return Result::Ok;
}

[[nodiscard]] Result ProbeOpusEncoder(QString &detail) {
	// Voice messages died here: upstream hardcodes FLTP, which only the native
	// (experimental) encoder takes, while libopus advertises S16/FLT and
	// avcodec_open2 rejects anything unlisted. Mirror media_audio_capture's
	// adaptive choice so a regression there fails the build, not a user.
	FFmpeg::EnsureRegistered();
	const auto codec = avcodec_find_encoder_by_name("libopus");
	if (!codec) {
		detail = u"libopus encoder not found"_q;
		return Result::Fail;
	}
	auto context = avcodec_alloc_context3(codec);
	if (!context) {
		detail = u"avcodec_alloc_context3 failed"_q;
		return Result::Fail;
	}
	const auto guard = gsl::finally([&] { avcodec_free_context(&context); });

	context->sample_fmt = AV_SAMPLE_FMT_FLTP;
	if (const auto formats = codec->sample_fmts) {
		auto supported = false;
		auto first = AV_SAMPLE_FMT_NONE;
		for (auto i = 0; formats[i] != AV_SAMPLE_FMT_NONE; ++i) {
			if (first == AV_SAMPLE_FMT_NONE) {
				first = formats[i];
			}
			if (formats[i] == AV_SAMPLE_FMT_FLTP) {
				supported = true;
				break;
			}
		}
		if (!supported && first != AV_SAMPLE_FMT_NONE) {
			context->sample_fmt = first;
		}
	}
	context->bit_rate = 32000;
	context->sample_rate = 48000;
	context->channels = 1;
	context->channel_layout = AV_CH_LAYOUT_MONO;

	const auto error = avcodec_open2(context, codec, nullptr);
	if (error < 0) {
		detail = u"avcodec_open2 %1 for sample_fmt %2"_q
			.arg(error)
			.arg(int(context->sample_fmt));
		return Result::Fail;
	}
	detail = u"libopus opened with sample_fmt %1"_q.arg(int(context->sample_fmt));
	return Result::Ok;
}

[[nodiscard]] Result ProbeLottie(QString &detail) {
	// rlottie is where opening a chat froze the whole app: its threaded scheduler
	// deadlocked inside a magic static. Rendering two frames here is what the
	// watchdog above is really guarding.
	auto file = QFile(u":/icons/notify_toggle.lottie"_q);
	if (!file.open(QIODevice::ReadOnly)) {
		detail = u"resource missing"_q;
		return Result::Fail;
	}
	const auto content = Lottie::ReadContent(file.readAll(), QString());
	if (content.isEmpty()) {
		detail = u"empty content"_q;
		return Result::Fail;
	}
	auto generator = Lottie::FrameGenerator(content);
	const auto count = generator.count();
	if (count <= 0) {
		detail = u"rlottie parsed 0 frames"_q;
		return Result::Fail;
	}
	auto rendered = 0;
	for (auto i = 0; i != 2; ++i) {
		auto frame = generator.renderNext(QImage(), QSize(64, 64));
		if (!frame.image.isNull()) {
			++rendered;
		}
	}
	if (!rendered) {
		detail = u"no frame rendered out of %1"_q.arg(count);
		return Result::Fail;
	}
	detail = u"%1 frames, rendered %2"_q.arg(count).arg(rendered);
	return Result::Ok;
}

[[nodiscard]] Result ProbeQtImages(QString &detail) {
	// The static image plugins are linked, not loaded: when one of them stops
	// being registered the app shows empty stickers and backgrounds instead of
	// failing to start, which is exactly the kind of thing a screenshot misses.
	const auto files = {
		std::pair{ ":/gui/art/cocoon.webp", "webp" },
		std::pair{ ":/gui/art/bg_thumbnail.png", "png" },
		std::pair{ ":/gui/art/bg_initial.jpg", "jpeg" },
	};
	auto failed = QStringList();
	auto sizes = QStringList();
	for (const auto &[path, name] : files) {
		auto file = QFile(QString::fromUtf8(path));
		if (!file.open(QIODevice::ReadOnly)) {
			failed.push_back(u"%1:missing"_q.arg(name));
			continue;
		}
		const auto image = QImage::fromData(file.readAll());
		if (image.isNull()) {
			failed.push_back(u"%1:decode"_q.arg(name));
		} else {
			sizes.push_back(u"%1 %2x%3"_q
				.arg(name)
				.arg(image.width())
				.arg(image.height()));
		}
	}
	if (!failed.isEmpty()) {
		detail = failed.join(',');
		return Result::Fail;
	}
	detail = sizes.join(", ");
	return Result::Ok;
}

[[nodiscard]] Result ProbeOpenAl(QString &detail) {
	// A CI runner has no sound card, so an absent device is a SKIP - the point is
	// that the statically linked OpenAL initialises at all where one exists.
	// Only where a device is actually expected. This build is compiled against the
	// XP CRT and its OpenAL opens the XP-era backends; on a modern Windows -- the
	// CI runner, this workstation -- alcOpenDevice() walks into the current audio
	// stack and dies inside it. That says nothing about XP, so do not pretend to
	// test it here. LOBYTE(LOWORD(GetVersion())) is 5 on XP/2003 and >= 6 since
	// Vista, which is all this needs to know.
	if (LOBYTE(LOWORD(GetVersion())) != 5) {
		detail = u"not Windows XP - the XP audio stack is not here to test"_q;
		return Result::Skip;
	}
	const auto device = alcOpenDevice(nullptr);
	if (!device) {
		detail = u"no playback device"_q;
		return Result::Skip;
	}
	const auto context = alcCreateContext(device, nullptr);
	if (!context) {
		alcCloseDevice(device);
		detail = u"alcCreateContext failed"_q;
		return Result::Fail;
	}
	const auto name = alcGetString(device, ALC_DEVICE_SPECIFIER);
	detail = name ? QString::fromUtf8(name) : u"unnamed device"_q;
	alcDestroyContext(context);
	alcCloseDevice(device);
	return Result::Ok;
}

struct Probe {
	const char *name = nullptr;
	Result (*run)(QString &detail) = nullptr;
};

[[nodiscard]] const std::vector<Probe> &Probes() {
	static const auto result = std::vector<Probe>{
		{ "ffmpeg_registry", ProbeFfmpegRegistry },
		{ "opus_encoder", ProbeOpusEncoder },
		{ "lottie_render", ProbeLottie },
		{ "qt_images", ProbeQtImages },
		{ "openal_device", ProbeOpenAl },
	};
	return result;
}

// The port builds a /SUBSYSTEM:WINDOWS,5.01 binary, so it has no console of its
// own; without this the report would only ever reach the file.
void AttachToParentConsole() {
	if (AttachConsole(ATTACH_PARENT_PROCESS)) {
		auto unused = (FILE*)nullptr;
		freopen_s(&unused, "CONOUT$", "w", stdout);
		freopen_s(&unused, "CONOUT$", "w", stderr);
	}
}

[[nodiscard]] int Run(const QString &reportPath) {
	GlobalReportPath = reportPath;
	if (!reportPath.isEmpty()) {
		QFile::remove(reportPath);
	}
	AttachToParentConsole();
	StartWatchdog();

	auto report = Report();
	WriteLine(u"XPSELFTEST BEGIN probes=%1"_q.arg(Probes().size()));
	for (const auto &probe : Probes()) {
		GlobalStage = QString::fromUtf8(probe.name);
		GlobalProbeDeadline = crl::now() + kProbeTimeoutMs;
		auto timer = QElapsedTimer();
		timer.start();

		auto detail = QString();
		auto result = Result::Fail;
		try {
			result = probe.run(detail);
		} catch (const std::exception &e) {
			detail = u"exception: %1"_q.arg(QString::fromUtf8(e.what()));
		} catch (...) {
			detail = u"unknown exception"_q;
		}
		GlobalProbeDeadline = 0;

		const auto status = (result == Result::Ok)
			? u"OK"_q
			: (result == Result::Skip)
			? u"SKIP"_q
			: u"FAIL"_q;
		((result == Result::Ok)
			? report.ok
			: (result == Result::Skip)
			? report.skip
			: report.fail) += 1;
		WriteLine(u"XPSELFTEST stage=%1 status=%2 ms=%3 detail=%4"_q
			.arg(GlobalStage)
			.arg(status)
			.arg(timer.elapsed())
			.arg(detail));
	}
	GlobalFinished = true;

	const auto code = report.fail ? 1 : 0;
	WriteLine(u"XPSELFTEST RESULT ok=%1 skip=%2 fail=%3 code=%4"_q
		.arg(report.ok)
		.arg(report.skip)
		.arg(report.fail)
		.arg(code));
	return code;
}

} // namespace

std::optional<int> MaybeRun(int argc, char *argv[]) {
	auto reportPath = QString();
	auto requested = false;
	for (auto i = 1; i != argc; ++i) {
		const auto argument = QString::fromLocal8Bit(argv[i]);
		if (argument == u"-xpselftest"_q || argument == u"--xpselftest"_q) {
			requested = true;
			if (i + 1 != argc) {
				const auto next = QString::fromLocal8Bit(argv[i + 1]);
				if (!next.startsWith('-')) {
					reportPath = next;
				}
			}
		}
	}
	if (!requested) {
		return std::nullopt;
	}
	// QCoreApplication, not the full Application: the probes must not depend on a
	// window, an account or the working directory, so they can run on the CI
	// runner right after the link, minutes before anything reaches the VM. Qt
	// still needs an instance for its resource and plugin machinery.
	auto app = QCoreApplication(argc, argv);
	return Run(reportPath);
}

} // namespace Platform::XpSelfTest
