/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_location.h"

#include "core/application.h"
#include "core/shortcuts.h"
#include "data/business/data_business_info.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "settings/business/settings_recipients_helper.h"
#include "settings/settings_common.h"
#include "storage/storage_account.h"
#include "ui/controls/location_picker.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

class Location : public Section<Location> {
public:
	Location(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~Location();

	[[nodiscard]] rpl::producer<QString> title() override;

	const Ui::RoundRect *bottomSkipRounding() const override {
		return mapSupported() ? nullptr : &_bottomSkipRounding;
	}

private:
	void setupContent(not_null<Window::SessionController*> controller);
	void save();

	void setupPicker(not_null<Ui::VerticalLayout*> content);
	void setupUnsupported(not_null<Ui::VerticalLayout*> content);

	[[nodiscard]] bool mapSupported() const;
	void chooseOnMap();

	const Ui::LocationPickerConfig _config;
	rpl::variable<Data::BusinessLocation> _data;
	rpl::variable<Data::CloudImage*> _map = nullptr;
	base::weak_ptr<Ui::LocationPicker> _picker;
	std::shared_ptr<QImage> _view;
	Ui::RoundRect _bottomSkipRounding;

};

[[nodiscard]] Ui::LocationPickerConfig ResolveBusinessMapsConfig(
		not_null<Main::Session*> session) {
	const auto &appConfig = session->appConfig();
	auto map = appConfig.get<base::flat_map<QString, QString>>(
		u"tdesktop_config_map"_q,
		base::flat_map<QString, QString>());
	return { // XP walk: designated -> positional (C7555)
		map[u"bmaps"_q], // mapsToken
		map[u"bgeo"_q], // geoToken
	};
}

Location::Location(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _config(ResolveBusinessMapsConfig(&controller->session()))
, _bottomSkipRounding(st::boxRadius, st::boxDividerBg) {
	setupContent(controller);
}

Location::~Location() {
	if (!Core::Quitting()) {
		save();
	}
}

rpl::producer<QString> Location::title() {
	return tr::lng_location_title();
}

void Location::setupContent(
		not_null<Window::SessionController*> controller) {
	using namespace rpl::mappers;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	if (mapSupported()) {
		setupPicker(content);
	} else {
		setupUnsupported(content);
	}

	Ui::ResizeFitChild(this, content);
}

void Location::setupPicker(not_null<Ui::VerticalLayout*> content) {
	_data = controller()->session().user()->businessDetails().location;

	AddDividerTextWithLottie(content, {
		// XP walk: designated -> positional (C7555)
		u"location"_q, // lottie
		{}, // lottieRepeat
		st::settingsCloudPasswordIconSize, // lottieSize
		st::peerAppearanceIconPadding, // lottieMargins
		showFinishes(), // showFinished
		tr::lng_location_about(Ui::Text::WithEntities), // about
		st::peerAppearanceCoverLabelMargin, // aboutMargins
	});

	const auto address = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::settingsLocationAddress,
			Ui::InputField::Mode::MultiLine,
			tr::lng_location_address(),
			_data.current().address),
		st::settingsChatbotsUsernameMargins);

	_data.value(
	) | rpl::on_next([=](const Data::BusinessLocation &location) {
		address->setText(location.address);
	}, address->lifetime());

	address->changes() | rpl::on_next([=] {
		auto copy = _data.current();
		copy.address = address->getLastText();
		_data = std::move(copy);
		}, address->lifetime());

	AddDivider(content);
	AddSkip(content);

	const auto maptoggle = AddButtonWithIcon(
		content,
		tr::lng_location_set_map(),
		st::settingsButton,
		{ &st::menuIconAddress }
	)->toggleOn(_data.value(
	) | rpl::map([](const Data::BusinessLocation &location) {
		return location.point.has_value();
	}));

	maptoggle->toggledValue() | rpl::on_next([=](bool toggled) {
		if (!toggled) {
			auto copy = _data.current();
			if (copy.point.has_value()) {
				copy.point = std::nullopt;
				_data = std::move(copy);
			}
		} else if (!_data.current().point.has_value()) {
			_data.force_assign(_data.current());
			chooseOnMap();
		}
	}, maptoggle->lifetime());

	const auto mapSkip = st::defaultVerticalListSkip;
	const auto mapWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::AbstractButton>>(
			content,
			object_ptr<Ui::AbstractButton>(content),
			st::boxRowPadding + QMargins(0, mapSkip, 0, mapSkip)));
	mapWrap->toggle(_data.current().point.has_value(), anim::type::instant);

	const auto map = mapWrap->entity();
	map->resize(map->width(), st::locationSize.height());

	_data.value(
	) | rpl::on_next([=](const Data::BusinessLocation &location) {
		const auto image = location.point.has_value()
			? controller()->session().data().location(*location.point).get()
			: nullptr;
		if (image) {
			image->load(&controller()->session(), {});
			_view = image->createView();
		}
		mapWrap->toggle(image != nullptr, anim::type::normal);
		_map = image;
	}, mapWrap->lifetime());

	map->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(map);

		const auto left = (map->width() - st::locationSize.width()) / 2;
		const auto rect = QRect(QPoint(left, 0), st::locationSize);
		const auto &image = _view ? *_view : QImage();
		if (!image.isNull()) {
			p.drawImage(rect, image);
		}

		const auto paintMarker = [&](const style::icon &icon) {
			icon.paint(
				p,
				rect.x() + ((rect.width() - icon.width()) / 2),
				rect.y() + (rect.height() / 2) - icon.height(),
				width());
			};
		paintMarker(st::historyMapPoint);
		paintMarker(st::historyMapPointInner);
	}, map->lifetime());

	controller()->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		map->update();
	}, map->lifetime());

	map->setClickedCallback([=] {
		chooseOnMap();
	});

	showFinishes() | rpl::on_next([=] {
		address->setFocus();
	}, address->lifetime());
}

void Location::chooseOnMap() {
	if (const auto strong = _picker.get()) {
		strong->activate();
		return;
	}
	const auto callback = [=](Data::InputVenue venue) {
		auto copy = _data.current();
		copy.point = Data::LocationPoint(
			venue.lat,
			venue.lon,
			Data::LocationPoint::NoAccessHash);
		copy.address = venue.address;
		_data = std::move(copy);
	};
	const auto session = &controller()->session();
	const auto current = _data.current().point;
	// XP walk: designated -> positional (C7555). GeoLocation: point, bounds, accuracy.
	const auto initial = current
		? Core::GeoLocation{
			{ current->lat(), current->lon() }, // point
			{}, // bounds
			Core::GeoLocationAccuracy::Exact, // accuracy
		}
		: Core::GeoLocation();
	// XP walk: designated -> positional (C7555). LocationPicker::Descriptor: parent,
	// config, chooseLabel, recipient, session, initial, callback, quit, storageId,
	// closeRequests. recipient@3 gap-filled nullptr (theirs omits it).
	_picker = Ui::LocationPicker::Show({
		controller()->widget(), // parent
		_config, // config
		tr::lng_maps_point_set(), // chooseLabel
		nullptr, // recipient
		session, // session
		initial, // initial
		crl::guard(this, callback), // callback
		[] { Shortcuts::Launch(Shortcuts::Command::Quit); }, // quit
		session->local().resolveStorageIdBots(), // storageId
		death(), // closeRequests
	});
}

void Location::setupUnsupported(not_null<Ui::VerticalLayout*> content) {
	AddDividerTextWithLottie(content, { // XP walk: designated -> positional (C7555)
		u"phone"_q, // lottie
		{}, // lottieRepeat (gap-fill default)
		st::settingsCloudPasswordIconSize, // lottieSize
		st::peerAppearanceIconPadding, // lottieMargins
		showFinishes(), // showFinished
		tr::lng_location_fallback(Ui::Text::WithEntities), // about
		st::peerAppearanceCoverLabelMargin, // aboutMargins
		RectPart::Top, // parts
	});
}

void Location::save() {
	const auto fail = [=](QString error) {
	};
	auto value = _data.current();
	value.address = value.address.trimmed();
	controller()->session().data().businessInfo().saveLocation(value, fail);
}

bool Location::mapSupported() const {
	return Ui::LocationPicker::Available(_config);
}

} // namespace

Type LocationId() {
	return Location::Id();
}

} // namespace Settings
