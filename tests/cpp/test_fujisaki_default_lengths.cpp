// A Fujisaki command that gives no filter length gets the DSP's default (#155).
//
// frame.h and pack.h say 0 means "the DSP's default" for fujisakiPhraseLen
// and fujisakiAccentLen.  The DSP designed a filter only when a command gave
// a length and otherwise kept the last one: once any clause had sent a
// length of its own, every later command that sent 0 played with that
// length.  The frontend now sizes the lengths of a statement, an exclamation
// and a question to the clause and the syllable, and leaves a comma clause's
// at 0; one DSP speaks clause after clause.

#include "doctest.h"

#include "pitchModel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace {

const int kSampleRates[] = {11025, 16000, 22050, 44100};

std::vector<double> run(FujisakiBartmanPitch& model, int samples) {
	std::vector<double> out;
	out.reserve(samples);
	for (int i = 0; i < samples; ++i) out.push_back(model.processMultiplier());
	return out;
}

} // namespace

TEST_CASE("Fujisaki: a phrase command without a length uses the default after one with a length") {
	for (int sr : kSampleRates) {
		CAPTURE(sr);
		const int n = sr / 2;

		FujisakiBartmanPitch fresh(sr);
		fresh.phrase(0.3, 0);
		const std::vector<double> expected = run(fresh, n);

		FujisakiBartmanPitch used(sr);
		used.phrase(0.3, sr / 100);  // a short clause: 10 ms to the peak
		run(used, n);
		used.resetPast();            // the next clause
		used.phrase(0.3, 0);
		const std::vector<double> got = run(used, n);

		double worst = 0.0;
		for (int i = 0; i < n; ++i) worst = std::max(worst, std::fabs(got[i] - expected[i]));
		CHECK(worst < 1e-12);
	}
}

TEST_CASE("Fujisaki: an accent command without a length uses the default after one with a length") {
	for (int sr : kSampleRates) {
		CAPTURE(sr);
		const int n = sr / 2;

		FujisakiBartmanPitch fresh(sr);
		fresh.accent(0.2, 0, 0);
		const std::vector<double> expected = run(fresh, n);

		FujisakiBartmanPitch used(sr);
		used.accent(0.2, sr / 50, sr / 200);  // 20 ms command, 5 ms to the peak
		run(used, n);
		used.resetPast();
		used.accent(0.2, 0, 0);
		const std::vector<double> got = run(used, n);

		double worst = 0.0;
		for (int i = 0; i < n; ++i) worst = std::max(worst, std::fabs(got[i] - expected[i]));
		CHECK(worst < 1e-12);
	}
}

TEST_CASE("Fujisaki: a length a command gives is still used") {
	for (int sr : kSampleRates) {
		CAPTURE(sr);
		// The phrase component peaks one length after the command.
		FujisakiBartmanPitch model(sr);
		const int len = sr / 20;  // 50 ms
		model.phrase(0.3, len);
		const std::vector<double> y = run(model, sr / 2);
		int peak = 0;
		for (int i = 1; i < (int)y.size(); ++i) if (y[i] > y[peak]) peak = i;
		CHECK(std::abs(peak - len) <= 2);
	}
}
