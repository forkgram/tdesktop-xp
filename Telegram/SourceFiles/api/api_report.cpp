/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_report.h"

#include "apiwrap.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_report.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/report_box_graphics.h"
#include "ui/layers/show.h"

namespace Api {

namespace {

MTPreportReason ReasonToTL(const Ui::ReportReason &reason) {
	using Reason = Ui::ReportReason;
	switch (reason) {
	case Reason::Spam: return MTP_inputReportReasonSpam();
	case Reason::Fake: return MTP_inputReportReasonFake();
	case Reason::Violence: return MTP_inputReportReasonViolence();
	case Reason::ChildAbuse: return MTP_inputReportReasonChildAbuse();
	case Reason::Pornography: return MTP_inputReportReasonPornography();
	case Reason::Copyright: return MTP_inputReportReasonCopyright();
	case Reason::IllegalDrugs: return MTP_inputReportReasonIllegalDrugs();
	case Reason::PersonalDetails:
		return MTP_inputReportReasonPersonalDetails();
	case Reason::Other: return MTP_inputReportReasonOther();
	}
	Unexpected("Bad reason group value.");
}

} // namespace

void SendPhotoReport(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		Ui::ReportReason reason,
		const QString &comment,
		not_null<PhotoData*> photo) {
	peer->session().api().request(MTPaccount_ReportProfilePhoto(
		peer->input(),
		photo->mtpInput(),
		ReasonToTL(reason),
		MTP_string(comment)
	)).done([=] {
		show->showToast(tr::lng_report_thanks(tr::now));
	}).send();
}

auto CreateReportMessagesOrStoriesCallback(
	std::shared_ptr<Ui::Show> show,
	not_null<PeerData*> peer)
-> Fn<void(Data::ReportInput, Fn<void(ReportResult)>)> {
	using TLChoose = MTPDreportResultChooseOption;
	using TLAddComment = MTPDreportResultAddComment;
	using TLReported = MTPDreportResultReported;
	using Result = ReportResult;

	struct State final {
#ifdef _DEBUG
		~State() {
			qDebug() << "Messages or Stories Report ~State().";
		}
#endif
		mtpRequestId requestId = 0;
	};
	const auto state = std::make_shared<State>();

	return [=](
			Data::ReportInput reportInput,
			Fn<void(Result)> done) {
		auto apiIds = QVector<MTPint>();
		apiIds.reserve(reportInput.ids.size() + reportInput.stories.size());
		for (const auto &id : reportInput.ids) {
			apiIds.push_back(MTP_int(id));
		}
		for (const auto &story : reportInput.stories) {
			apiIds.push_back(MTP_int(story));
		}

		const auto received = [=](
				const MTPReportResult &result,
				mtpRequestId requestId) {
			if (state->requestId != requestId) {
				return;
			}
			state->requestId = 0;
			done(result.match([&](const TLChoose &data) {
				const auto t = qs(data.vtitle());
				auto list = Result::Options();
				list.reserve(data.voptions().v.size());
				for (const auto &tl : data.voptions().v) {
					list.emplace_back(Result::Option{
						// XP walk: designated -> positional (C7555).
						tl.data().voption().v, // id
						qs(tl.data().vtext()), // text
					});
				}
				// XP walk: designated -> positional (C7555).
				return Result{ std::move(list), t }; // options, title
			}, [&](const TLAddComment &data) -> Result {
				// XP walk: designated -> named-local (C7555; sets only
				// commentOption). Named `output` to avoid shadowing the
				// enclosing `result` (MTPReportResult) parameter.
				auto output = Result();
				output.commentOption = ReportResult::CommentOption{
					data.is_optional(), // optional
					data.voption().v, // id
				};
				return output;
			}, [&](const TLReported &data) -> Result {
				// XP walk: designated -> named-local (C7555; sets only
				// successful). Named `output` to avoid shadowing `result`.
				auto output = Result();
				output.successful = true;
				return output;
			}));
		};

		const auto fail = [=](const MTP::Error &error) {
			state->requestId = 0;
			// XP walk: designated -> named-local (C7555; sets only error).
			auto result = Result();
			result.error = error.type();
			done(result);
		};

		if (!reportInput.stories.empty()) {
			state->requestId = peer->session().api().request(
				MTPstories_Report(
					peer->input(),
					MTP_vector<MTPint>(apiIds),
					MTP_bytes(reportInput.optionId),
					MTP_string(reportInput.comment))
			).done(received).fail(fail).send();
		} else {
			state->requestId = peer->session().api().request(
				MTPmessages_Report(
					peer->input(),
					MTP_vector<MTPint>(apiIds),
					MTP_bytes(reportInput.optionId),
					MTP_string(reportInput.comment))
			).done(received).fail(fail).send();
		}
	};
}

} // namespace Api
