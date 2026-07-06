/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
//
// XP-walk stub. v5.14.0 introduced td_tde2e (end-to-end encryption for group
// calls). Its real implementation pulls in TDLib's C++ end-to-end backend (the
// desktop-app external td library), which is not available in the Windows XP
// build. Group calls also require WebRTC, which is disabled on XP
// (DESKTOP_APP_DISABLE_WEBRTC_INTEGRATION), so TdE2E is never invoked at
// runtime here - it only has to compile and link. Every out-of-line method
// declared in the module header is replaced below with a trivial no-op so the
// module links without the unavailable TDLib dependency.
//
#include "tde2e/tde2e_api.h"

#include "base/assertion.h"

namespace TdE2E {

auto EncryptDecrypt::callback()
-> Fn<EncryptionBuffer(const EncryptionBuffer&, int64_t, bool, int32_t)> {
	return [](
			const EncryptionBuffer &,
			int64_t,
			bool,
			int32_t) -> EncryptionBuffer {
		return {};
	};
}

void EncryptDecrypt::setCallId(CallId) {
}

void EncryptDecrypt::clearCallId(CallId) {
}

Call::Call(UserId myUserId)
: _myUserId(myUserId) {
}

Call::~Call() = default;

PublicKey Call::myKey() const {
	return {};
}

void Call::joined() {
}

void Call::apply(
		int,
		int,
		const std::vector<Block> &,
		bool) {
}

rpl::producer<Call::SubchainRequest> Call::subchainRequests() const {
	return _subchainRequests.events();
}

void Call::subchainBlocksRequestFinished(int) {
}

rpl::producer<QByteArray> Call::sendOutboundBlock() const {
	return _outboundBlocks.events();
}

std::optional<CallFailure> Call::failed() const {
	return {};
}

rpl::producer<CallFailure> Call::failures() const {
	return _failures.events();
}

QByteArray Call::emojiHash() const {
	return {};
}

rpl::producer<QByteArray> Call::emojiHashValue() const {
	return _emojiHash.value();
}

bool Call::hasLastBlock0() const {
	return false;
}

void Call::refreshLastBlock0(std::optional<Block>) {
}

Block Call::makeJoinBlock() {
	return {};
}

Block Call::makeRemoveBlock(const base::flat_set<UserId> &) {
	return {};
}

rpl::producer<ParticipantsSet> Call::participantsSetValue() const {
	return _participantsSet.value();
}

void Call::registerEncryptDecrypt(std::shared_ptr<EncryptDecrypt>) {
}

rpl::lifetime &Call::lifetime() {
	return _lifetime;
}

void Call::setId(CallId) {
}

void Call::apply(int, const Block &) {
}

void Call::fail(CallFailure) {
}

void Call::checkForOutboundMessages() {
}

void Call::checkWaitingBlocks(int, bool) {
}

void Call::shortPoll(int) {
}

std::int64_t Call::libId() const {
	return 0;
}

} // namespace TdE2E
