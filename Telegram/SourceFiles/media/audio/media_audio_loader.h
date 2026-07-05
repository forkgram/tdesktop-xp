/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"
#include "core/file_location.h"
#include "media/streaming/media_streaming_utility.h"

namespace Media {

class AudioPlayerLoader {
public:
	AudioPlayerLoader(
		const Core::FileLocation &file,
		const QByteArray &data,
		bytes::vector &&buffer);
	virtual ~AudioPlayerLoader();

	virtual bool check(
		const Core::FileLocation &file,
		const QByteArray &data);

	virtual bool open(crl::time positionMs, float64 speed = 1.) = 0;
	virtual crl::time duration() = 0;
	virtual int samplesFrequency() = 0;
	virtual int sampleSize() = 0;
	virtual int format() = 0;

	virtual void dropFramesTill(int64 samples) {
	}
	[[nodiscard]] virtual int64 startReadingQueuedFrames(float64 newSpeed) {
		Unexpected(
			"startReadingQueuedFrames() on not AbstractAudioFFMpegLoader");
	}

	[[nodiscard]] int bytesPerBuffer();

	enum class ReadError {
		Other,
		Retry,
		RetryNotQueued,
		Wait,
		EndOfFile,
	};
	using ReadResult = std::variant<bytes::const_span, ReadError>;
	[[nodiscard]] virtual ReadResult readMore() = 0;

	virtual void enqueuePackets(std::deque<FFmpeg::Packet> &&packets) {
		Unexpected("enqueuePackets() call on not ChildFFMpegLoader.");
	}
	virtual void setForceToBuffer(bool force) {
		Unexpected("setForceToBuffer() call on not ChildFFMpegLoader.");
	}
	virtual bool forceToBuffer() const {
		return false;
	}

	void saveDecodedSamples(not_null<QByteArray*> samples);
	void takeSavedDecodedSamples(not_null<QByteArray*> samples);
	bool holdsSavedDecodedSamples() const;
	void dropDecodedSamples();

protected:
	Core::FileLocation _file;
	bool _access = false;
	QByteArray _data;
	bytes::vector _bytes;

	QFile _f;
	int _dataPos = 0;

	bool openFile();

private:
	QByteArray _savedSamples;
	bool _holdsSavedSamples = false;

	int _bytesPerBuffer = 0;

};

// XP walk: ReadResult is std::variant<bytes::const_span, ReadError> and the
// generic variant operator== in base/variant.h requires every alternative to
// be equality-comparable, which bytes::const_span is not (C2678). Provide
// explicit comparisons against ReadError that ignore the span alternative.
[[nodiscard]] inline bool operator==(
		const AudioPlayerLoader::ReadResult &a,
		AudioPlayerLoader::ReadError b) {
	const auto error = std::get_if<AudioPlayerLoader::ReadError>(&a);
	return error && (*error == b);
}

[[nodiscard]] inline bool operator!=(
		const AudioPlayerLoader::ReadResult &a,
		AudioPlayerLoader::ReadError b) {
	return !(a == b);
}

} // namespace Media
