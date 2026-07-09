/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_star.h"

#include "ui/effects/premium_star_renderer.h"
#include "ui/effects/premium_3d_support.h"
#include "ui/gl/gl_surface.h"
#include "base/call_delayed.h"
#include "base/random.h"

namespace Ui::Premium {
namespace {

constexpr auto kShimmerSpeed = 0.03;
constexpr auto kFadeInDuration = 0.22;
constexpr auto kDragYaw = 0.5;
constexpr auto kDragPitch = 0.3;
constexpr auto kMaxPitch = 50.;
constexpr auto kMaxFrameDelta = crl::time(66);
constexpr auto kMsInSecond = 1000.;
constexpr auto kEnterDelay = crl::time(200);
constexpr auto kEnterYaw = -180.;
constexpr auto kBackDuration = crl::time(600);
constexpr auto kIdleDelay = crl::time(2000);
constexpr auto kOvershootTension = 2.;

} // namespace

Star::Star(QWidget *parent)
: RpWidget(parent)
, _animation([=] { frame(); }) {
}

Star::~Star() = default;

bool Star::Supported() {
	return Object3dSupported();
}

void Star::setColors(QColor gradient1, QColor gradient2) {
	_gradient1 = gradient1;
	_gradient2 = gradient2;
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	if (_renderer) {
		_renderer->setColors(_gradient1, _gradient2);
	}
#endif
}

void Star::setGolden(bool golden) {
	_golden = golden;
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	if (_renderer) {
		_renderer->setGolden(_golden);
	}
#endif
}

void Star::setShownProgress(float64 progress) {
	progress = std::clamp(progress, 0., 1.);
	if (_opacity == progress) {
		return;
	}
	_opacity = progress;
	if (!_animation.animating()) {
		pushState();
		if (const auto w = surfaceWidget()) {
			w->update();
		}
	}
}

void Star::setPaused(bool paused) {
	if (_paused == paused) {
		return;
	}
	_paused = paused;
	if (_paused) {
		stopAnimation();
	} else if (!isHidden()) {
		startAnimation();
	}
}

rpl::producer<float64> Star::flungStrength() const {
	return _flung.events();
}

QWidget *Star::surfaceWidget() const {
	return _surface ? _surface->rpWidget() : nullptr;
}

void Star::ensureSurface() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	if (_surface) {
		return;
	}
	auto renderer = std::make_unique<StarRenderer>();
	_renderer = renderer.get();
	_renderer->setGolden(_golden);
	if (_gradient1.isValid() && _gradient2.isValid()) {
		_renderer->setColors(_gradient1, _gradient2);
	}
	_surface = GL::CreateSurface(
		this,
		GL::ChosenRenderer{
			.renderer = std::move(renderer),
			.backend = GL::Backend::QRhi,
		});
	if (const auto w = surfaceWidget()) {
		w->setAttribute(Qt::WA_TransparentForMouseEvents);
		w->setAttribute(Qt::WA_AlwaysStackOnTop);
		w->setGeometry(rect());
		w->hide();
	}
#endif // Qt >= 6.7
}

void Star::startAnimation() {
	if (_paused || _animation.animating()) {
		return;
	}
	_lastFrame = crl::now();
	_animation.start();
}

void Star::stopAnimation() {
	_animation.stop();
}

void Star::advance(float64 dt) {
	_shimmer += dt * kShimmerSpeed;
	_shimmer -= std::floor(_shimmer);
	if (_fadeIn < 1.) {
		_fadeIn = std::min(1., _fadeIn + dt / kFadeInDuration);
	}
}

void Star::applyChannel(Channel channel, float64 value) {
	switch (channel) {
	case Channel::Yaw: _yaw = value; break;
	case Channel::Pitch: _pitch = value; break;
	case Channel::Bob: _bob = value; break;
	}
}

void Star::tick(crl::time now) {
	const auto ease = [](Easing easing, float64 t) -> float64 {
		switch (easing) {
		case Easing::Linear:
			return t;
		case Easing::Default:
			return CubicBezier(0.25, 0.1, 0.25, 1., t);
		case Easing::EaseOut:
			return CubicBezier(0., 0., 0.58, 1., t);
		case Easing::EaseOutQuint:
			return CubicBezier(0.23, 1., 0.32, 1., t);
		case Easing::Overshoot: {
			const auto u = t - 1.;
			return u * u * ((kOvershootTension + 1.) * u + kOvershootTension)
				+ 1.;
		}
		}
		return t;
	};
	const auto valueAt = [](const Track &track, float64 eased) -> float64 {
		const auto last = int(track.values.size()) - 1;
		if (last <= 0) {
			return track.values.empty() ? 0. : track.values.front();
		} else if (last == 1) {
			return track.values[0]
				+ eased * (track.values[1] - track.values[0]);
		}
		const auto position = eased * last;
		const auto segment = std::clamp(
			int(std::floor(position)),
			0,
			last - 1);
		const auto local = position - segment;
		return track.values[segment]
			+ local * (track.values[segment + 1] - track.values[segment]);
	};

	if (_gesture) {
		const auto elapsed = now - _gesture->start;
		for (const auto &track : _gesture->tracks) {
			if (elapsed < track.delay) {
				continue;
			}
			const auto local = std::min(
				elapsed - track.delay,
				track.duration);
			const auto fraction = (track.duration > 0)
				? (local / float64(track.duration))
				: 1.;
			applyChannel(
				track.channel,
				valueAt(track, ease(track.easing, fraction)));
		}
		if (elapsed >= _gesture->total) {
			_yaw = 0.;
			_gesture.reset();
			scheduleIdle(kIdleDelay);
		}
	} else if (_idleAt && now >= _idleAt && !_dragging) {
		_idleAt = 0;
		startIdleGesture();
	}
}

void Star::pushState() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	if (_renderer) {
		_renderer->setState({
			.yaw = float(_yaw),
			.pitch = float(_pitch),
			.bob = float(_bob),
			.shimmer = float(_shimmer),
			.alpha = float(_fadeIn * _opacity),
		});
	}
#endif // Qt >= 6.7
}

void Star::frame() {
	const auto now = crl::now();
	const auto delta = std::clamp(
		now - _lastFrame,
		crl::time(1),
		kMaxFrameDelta);
	_lastFrame = now;
	advance(delta / kMsInSecond);
	tick(now);
	pushState();
	if (const auto w = surfaceWidget()) {
		w->update();
	}
}

void Star::play(Gesture gesture) {
	gesture.start = crl::now();
	gesture.total = 0;
	for (const auto &track : gesture.tracks) {
		gesture.total = std::max(gesture.total, track.delay + track.duration);
	}
	cancelIdle();
	_gesture = std::move(gesture);
}

void Star::scheduleIdle(crl::time delay) {
	_idleAt = crl::now() + delay;
}

void Star::cancelIdle() {
	_idleAt = 0;
}

void Star::startIdleGesture() {
	if (_idleBag.empty()) {
		_idleBag = { 0, 1, 2, 3, 4 };
		for (auto i = int(_idleBag.size()) - 1; i > 0; --i) {
			std::swap(_idleBag[i], _idleBag[base::RandomIndex(i + 1)]);
		}
	}
	const auto index = _idleBag.back();
	_idleBag.pop_back();
	switch (index) {
	case 0: pullGesture(); break;
	case 1: slowFlipGesture(); break;
	case 2: sleepGesture(); break;
	default: flipGesture(); break;
	}
}

void Star::startBackSpring() {
	const auto fromYaw = _yaw;
	const auto fromPitch = _pitch;
	const auto fromBob = _bob;
	auto gesture = Gesture();
	gesture.tracks.push_back({
		Channel::Yaw, // channel
		{ fromYaw, 0. }, // values
		crl::time(0), // delay
		kBackDuration, // duration
		Easing::Overshoot, // easing
	});
	gesture.tracks.push_back({
		Channel::Pitch, // channel
		{ fromPitch, 0. }, // values
		crl::time(0), // delay
		kBackDuration, // duration
		Easing::Overshoot, // easing
	});
	gesture.tracks.push_back({
		Channel::Bob, // channel
		{ fromBob, 0. }, // values
		crl::time(0), // delay
		kBackDuration, // duration
		Easing::Overshoot, // easing
	});
	play(std::move(gesture));
	const auto magnitude = std::abs(fromYaw + fromPitch);
	if (magnitude > 0.) {
		_flung.fire_copy(magnitude);
	}
	scheduleIdle(kIdleDelay);
}

void Star::pullGesture() {
	const auto variant = base::RandomIndex(4);
	auto gesture = Gesture();
	if (variant == 0) {
		gesture.tracks.push_back({
			Channel::Pitch, // channel
			{ 0., 48. }, // values
			crl::time(0), // delay
			crl::time(2300), // duration
			Easing::EaseOutQuint, // easing
		});
		gesture.tracks.push_back({
			Channel::Pitch, // channel
			{ 48., 0. }, // values
			crl::time(2300), // delay
			crl::time(500), // duration
			Easing::Overshoot, // easing
		});
	} else {
		const auto target = (variant == 2) ? -485. : 485.;
		gesture.tracks.push_back({
			Channel::Yaw, // channel
			{ 0., target }, // values
			crl::time(0), // delay
			crl::time(3000), // duration
			Easing::EaseOutQuint, // easing
		});
		gesture.tracks.push_back({
			Channel::Yaw, // channel
			{ target, 0. }, // values
			crl::time(3000), // delay
			crl::time(1000), // duration
			Easing::Overshoot, // easing
		});
	}
	play(std::move(gesture));
}

void Star::slowFlipGesture() {
	auto gesture = Gesture();
	gesture.tracks.push_back({
		Channel::Yaw, // channel
		{ 0., 360. }, // values
		crl::time(0), // delay
		crl::time(8000), // duration
		Easing::Default, // easing
	});
	play(std::move(gesture));
}

void Star::flipGesture() {
	auto gesture = Gesture();
	gesture.tracks.push_back({
		Channel::Yaw, // channel
		{ 0., 180. }, // values
		crl::time(0), // delay
		crl::time(600), // duration
		Easing::Default, // easing
	});
	gesture.tracks.push_back({
		Channel::Yaw, // channel
		{ 180., 360. }, // values
		crl::time(2000), // delay
		crl::time(600), // duration
		Easing::Default, // easing
	});
	play(std::move(gesture));
}

void Star::sleepGesture() {
	auto gesture = Gesture();
	gesture.tracks.push_back({
		Channel::Yaw, // channel
		{ 0., 184. }, // values
		crl::time(0), // delay
		crl::time(600), // duration
		Easing::EaseOut, // easing
	});
	gesture.tracks.push_back({
		Channel::Pitch, // channel
		{ 0., 50. }, // values
		crl::time(0), // delay
		crl::time(600), // duration
		Easing::EaseOut, // easing
	});
	gesture.tracks.push_back({
		Channel::Yaw, // channel
		{ 180., 0. }, // values
		crl::time(10000), // delay
		crl::time(800), // duration
		Easing::Overshoot, // easing
	});
	gesture.tracks.push_back({
		Channel::Pitch, // channel
		{ 60., 0. }, // values
		crl::time(10000), // delay
		crl::time(800), // duration
		Easing::Overshoot, // easing
	});
	gesture.tracks.push_back({
		Channel::Bob, // channel
		{ 0., 2., -3., 2., -1., 2., -3., 2., -1., 0. }, // values
		crl::time(0), // delay
		crl::time(10000), // duration
		Easing::Linear, // easing
	});
	play(std::move(gesture));
}

void Star::resizeEvent(QResizeEvent *e) {
	if (const auto w = surfaceWidget()) {
		w->setGeometry(rect());
	}
}

void Star::startEnter() {
	base::call_delayed(kEnterDelay, this, [=] {
		if (isHidden()) {
			return;
		}
		if (const auto w = surfaceWidget()) {
			w->show();
		}
		_yaw = kEnterYaw;
		_pitch = _bob = 0.;
		startAnimation();
		startBackSpring();
	});
}

void Star::showEvent(QShowEvent *e) {
	ensureSurface();
	_yaw = kEnterYaw;
	_pitch = _bob = 0.;
	_gesture.reset();
	cancelIdle();
	if (const auto w = surfaceWidget()) {
		w->hide();
	}
	startAnimation();
}

void Star::hideEvent(QHideEvent *e) {
	stopAnimation();
	_gesture.reset();
	cancelIdle();
	_yaw = _pitch = _bob = 0.;
}

void Star::mousePressEvent(QMouseEvent *e) {
	_dragging = true;
	_lastDragPos = e->pos();
	_gesture.reset();
	cancelIdle();
}

void Star::mouseMoveEvent(QMouseEvent *e) {
	if (!_dragging) {
		return;
	}
	const auto pos = e->pos();
	const auto shift = pos - _lastDragPos;
	_yaw += -shift.x() * kDragYaw;
	_pitch = std::clamp(
		_pitch - shift.y() * kDragPitch,
		-kMaxPitch,
		kMaxPitch);
	_lastDragPos = pos;
}

void Star::mouseReleaseEvent(QMouseEvent *e) {
	if (!_dragging) {
		return;
	}
	_dragging = false;
	startBackSpring();
}

} // namespace Ui::Premium
