/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_premium_option.h"

#include "ui/text/format_values.h"

namespace Api {

constexpr auto kDiscountDivider = 1.;

Data::PremiumSubscriptionOption CreateSubscriptionOption(
		int months,
		int monthlyAmount,
		int64 amount,
		const QString &currency,
		const QString &botUrl) {
	const auto discount = [&] {
		const auto percent = 1. - float64(amount) / (monthlyAmount * months);
		return std::round(percent * 100. / kDiscountDivider)
			* kDiscountDivider;
	}();
	return {
		// XP walk: designated -> positional (C7555). PremiumSubscriptionOption:
		// months, duration, discount, costPerMonth, costNoDiscount, costTotal,
		// total, botUrl. total@6 skipped -> {} (default empty QString).
		months, // months
		Ui::FormatTTL(months * 86400 * 31), // duration
		(discount > 0) // discount
			? QString::fromUtf8("\xe2\x88\x92%1%").arg(discount)
			: QString(),
		Ui::FillAmountAndCurrency( // costPerMonth
			amount / float64(months),
			currency),
		Ui::FillAmountAndCurrency( // costNoDiscount
			monthlyAmount * months,
			currency),
		Ui::FillAmountAndCurrency(amount, currency), // costTotal
		{}, // total
		botUrl, // botUrl
	};
}

} // namespace Api
