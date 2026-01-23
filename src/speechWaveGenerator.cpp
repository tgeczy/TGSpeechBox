/*
This file is a part of the NV Speech Player project. 
URL: https://bitbucket.org/nvaccess/speechplayer
Copyright 2014 NV Access Limited.
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2.0, as published by
the Free Software Foundation.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
This license can be found at:
http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
*/

/*
Based on klsyn-88, found at http://linguistics.berkeley.edu/phonlab/resources/
*/

#define _USE_MATH_DEFINES

#include <cassert>
#include <cmath>
#include <cstdlib>
#include "debug.h"
#include "utils.h"
#include "speechWaveGenerator.h"

using namespace std;

const double PITWO=M_PI*2;

class NoiseGenerator {
	private:
	double lastValue;

	public:
	NoiseGenerator(): lastValue(0.0) {};

	void reset() {
		lastValue=0.0;
	}

	double getNext() {
		// rand() returns a non-negative value, so using it directly yields strictly
		// positive noise and a significant DC bias after the one-pole filter.
		//
		// Center the random value at 0 ([-0.5, 0.5]) to avoid DC offset "thumps"
		// when the signal (especially turbulence) is faded in/out.
		lastValue=(((double)rand()/(double)RAND_MAX)-0.5)+0.75*lastValue;
		return lastValue;
	}

};

class FrequencyGenerator {
	private:
	int sampleRate;
	double lastCyclePos;

	public:
	FrequencyGenerator(int sr): sampleRate(sr), lastCyclePos(0) {}

	void reset() {
		lastCyclePos=0;
	}

	double getNext(double frequency) {
		double cyclePos=fmod((frequency/sampleRate)+lastCyclePos,1);
		lastCyclePos=cyclePos;
		return cyclePos;
	}

};

class VoiceGenerator {
	private:
	FrequencyGenerator pitchGen;
	FrequencyGenerator vibratoGen;
	NoiseGenerator aspirationGen;
	// State for source shaping / DC-blocking.
	double lastFlow;
	double lastVoicedIn;
	double lastVoicedOut;

	public:
	bool glottisOpen;
	VoiceGenerator(int sr): pitchGen(sr), vibratoGen(sr), aspirationGen(), lastFlow(0.0), lastVoicedIn(0.0), lastVoicedOut(0.0), glottisOpen(false) {};

	void reset() {
		pitchGen.reset();
		vibratoGen.reset();
		aspirationGen.reset();
		lastFlow=0.0;
		lastVoicedIn=0.0;
		lastVoicedOut=0.0;
		glottisOpen=false;
	}

	double getNext(const speechPlayer_frame_t* frame) {
		double vibrato=(sin(vibratoGen.getNext(frame->vibratoSpeed)*PITWO)*0.06*frame->vibratoPitchOffset)+1;
		double cyclePos=pitchGen.getNext(frame->voicePitch*vibrato);

		double aspiration=aspirationGen.getNext()*0.1;

		// glottalOpenQuotient is optional in many packs.
		// Keep a sensible default that preserves brightness (and ensures there is
		// still a closed phase for coefficient updates).
		double effectiveOQ = frame->glottalOpenQuotient;
		if (effectiveOQ <= 0.0) effectiveOQ = 0.4;
		if (effectiveOQ < 0.10) effectiveOQ = 0.10;
		if (effectiveOQ > 0.95) effectiveOQ = 0.95;

		glottisOpen = cyclePos >= effectiveOQ;

		double flow = 0.0;
		if(glottisOpen) {
			double openLen = 1.0 - effectiveOQ;
			if (openLen < 0.0001) openLen = 0.0001;
			double phase = (cyclePos - effectiveOQ) / openLen; // 0..1 across open phase

			// KLGLOTT88 / Rosenberg C-style pulse (polynomial rise, sharp closure at wrap).
			// This keeps the classic "robotic" buzz without additional post-EQ.
			flow = (3.0 * phase * phase) - (2.0 * phase * phase * phase);
		}

		// Scale the flow pulse into a classic-ish excitation range.
		// (Keep headroom to avoid the "clippy" feeling.)
		const double flowScale = 1.6;
		flow *= flowScale;

		// Add a small "radiation" component (first difference) to restore some edge
		// without a heavy post-EQ that would also boost fricatives.
		double dFlow = flow - lastFlow;
		lastFlow = flow;
		// Standard lip radiation is approximately a first derivative.
		// Keep this modest to avoid transient spikes that feel like compression.
		const double radiationMix = 1.0;
		double voicedSrc = flow + (dFlow * radiationMix);

		// Turbulence: scale by instantaneous flow so it ramps smoothly with the pulse.
		double turbulence = aspiration * frame->voiceTurbulenceAmplitude;
		if(glottisOpen) {
			double flow01 = flow / flowScale; // 0..1
			if(flow01 < 0.0) flow01 = 0.0;
			if(flow01 > 1.0) flow01 = 1.0;
			turbulence *= flow01;
		} else {
			turbulence = 0.0;
		}

		// Apply voice amplitude, and remove any residual DC from the voiced source
		// (low cutoff so this doesn't "thin" the sound).
		double voicedIn = (voicedSrc + turbulence) * frame->voiceAmplitude;
		const double dcPole = 0.9995;
		double voiced = voicedIn - lastVoicedIn + (dcPole * lastVoicedOut);
		lastVoicedIn = voicedIn;
		lastVoicedOut = voiced;

		double aspOut = aspiration * frame->aspirationAmplitude;
		return aspOut + voiced;
	}

};

class Resonator {
	private:
	//raw parameters
	int sampleRate;
	double frequency;
	double bandwidth;
	bool anti;
	//calculated parameters
	bool setOnce;
	double a, b, c;
	//Memory
	double p1, p2;

	public:
	Resonator(int sampleRate, bool anti=false) {
		this->sampleRate=sampleRate;
		this->anti=anti;
		this->setOnce=false;
		this->p1=0;
		this->p2=0;
	}

	void setParams(double frequency, double bandwidth) {
		if(!setOnce||(frequency!=this->frequency)||(bandwidth!=this->bandwidth)) {
			this->frequency=frequency;
			this->bandwidth=bandwidth;

			// Keep bandwidths "as-is" for clarity.
			// (Adding constant bandwidth can reduce boxiness, but it also smears consonants
			// and reduces the crisp, buzzy character we're aiming for.)
			double effectiveBandwidth = bandwidth;

			double r=exp(-M_PI/sampleRate*effectiveBandwidth);
			c=-(r*r);
			b=r*cos(PITWO/sampleRate*-frequency)*2.0;
			a=1.0-b-c;
			if(anti&&frequency!=0) {
				a=1.0/a;
				c*=-a;
				b*=-a;
			}
		}
		this->setOnce=true;
	}

	double resonate(double in, double frequency, double bandwidth, bool allowUpdate=true) {
		if(allowUpdate) setParams(frequency,bandwidth);
		double out=a*in+b*p1+c*p2;
		p2=p1;
		p1=anti?in:out;
		return out;
	}

	void reset() {
		p1=0;
		p2=0;
		setOnce=false;
	}

};

class CascadeFormantGenerator { 
	private:
	int sampleRate;
	Resonator r1, r2, r3, r4, r5, r6, rN0, rNP;

	public:
	CascadeFormantGenerator(int sr): sampleRate(sr), r1(sr), r2(sr), r3(sr), r4(sr), r5(sr), r6(sr), rN0(sr,true), rNP(sr) {};

	void reset() {
		r1.reset(); r2.reset(); r3.reset(); r4.reset(); r5.reset(); r6.reset(); rN0.reset(); rNP.reset();
	}

	double getNext(const speechPlayer_frame_t* frame, bool glottisOpen, double input) {
		input/=2.0;
		// Updating resonator coefficients while strongly driven can produce zipper/clicks.
		// However, freezing them all the way through the open phase can leave a few
		// "stale" samples at segment boundaries (e.g. stop onsets like "B"), which is
		// also audible. Allow updates when the excitation is tiny.
		const double kUpdateEps = 0.03;
		bool allowUpdate = (!glottisOpen) || (fabs(input) < kUpdateEps);
		double n0Output=rN0.resonate(input,frame->cfN0,frame->cbN0,allowUpdate);
		double output=calculateValueAtFadePosition(input,rNP.resonate(n0Output,frame->cfNP,frame->cbNP,allowUpdate),frame->caNP);
		output=r6.resonate(output,frame->cf6,frame->cb6,allowUpdate);
		output=r5.resonate(output,frame->cf5,frame->cb5,allowUpdate);
		output=r4.resonate(output,frame->cf4,frame->cb4,allowUpdate);
		output=r3.resonate(output,frame->cf3,frame->cb3,allowUpdate);
		output=r2.resonate(output,frame->cf2,frame->cb2,allowUpdate);
		output=r1.resonate(output,frame->cf1,frame->cb1,allowUpdate);
		return output;
	}

};

class ParallelFormantGenerator { 
	private:
	int sampleRate;
	Resonator r1, r2, r3, r4, r5, r6;

	public:
	ParallelFormantGenerator(int sr): sampleRate(sr), r1(sr), r2(sr), r3(sr), r4(sr), r5(sr), r6(sr) {};

	void reset() {
		r1.reset(); r2.reset(); r3.reset(); r4.reset(); r5.reset(); r6.reset();
	}

	double getNext(const speechPlayer_frame_t* frame, bool glottisOpen, double input) {
		input/=2.0;
		const double kUpdateEps = 0.06;
		bool allowUpdate = (!glottisOpen) || (fabs(input) < kUpdateEps);
		double output=0;
		output+=(r1.resonate(input,frame->pf1,frame->pb1,allowUpdate)-input)*frame->pa1;
		output+=(r2.resonate(input,frame->pf2,frame->pb2,allowUpdate)-input)*frame->pa2;
		output+=(r3.resonate(input,frame->pf3,frame->pb3,allowUpdate)-input)*frame->pa3;
		output+=(r4.resonate(input,frame->pf4,frame->pb4,allowUpdate)-input)*frame->pa4;
		output+=(r5.resonate(input,frame->pf5,frame->pb5,allowUpdate)-input)*frame->pa5;
		output+=(r6.resonate(input,frame->pf6,frame->pb6,allowUpdate)-input)*frame->pa6;
		return calculateValueAtFadePosition(output,input,frame->parallelBypass);
	}

};

class SpeechWaveGeneratorImpl: public SpeechWaveGenerator {
	private:
	int sampleRate;
	VoiceGenerator voiceGenerator;
	NoiseGenerator fricGenerator;
	CascadeFormantGenerator cascade;
	ParallelFormantGenerator parallel;
	FrameManager* frameManager;
	double lastInput;
	double lastOutput;
	bool wasSilence;

	// Tiny attack smoothing for preFormantGain (helps stop onsets like "B" in letter echo).
	double smoothPreGain;
	double preGainAttackAlpha;



	public:
	SpeechWaveGeneratorImpl(int sr): sampleRate(sr), voiceGenerator(sr), fricGenerator(), cascade(sr), parallel(sr), frameManager(NULL), lastInput(0.0), lastOutput(0.0), wasSilence(true), smoothPreGain(0.0), preGainAttackAlpha(0.0) {
		// Tiny attack smoothing for preFormantGain (helps stop onsets like "B" in letter echo).
		const double attackMs = 1.0;
		preGainAttackAlpha = 1.0 - exp(-1.0 / (sampleRate * (attackMs * 0.001)));
	}

	unsigned int generate(const unsigned int sampleCount, sample* sampleBuf) {
		if(!frameManager) return 0; 
		for(unsigned int i=0;i<sampleCount;++i) {
			const speechPlayer_frame_t* frame=frameManager->getCurrentFrame();
			if(frame) {
				if(wasSilence) {
					voiceGenerator.reset();
					fricGenerator.reset();
					cascade.reset();
					parallel.reset();
					lastInput=0.0;
					lastOutput=0.0;
					smoothPreGain=0.0;
					wasSilence=false;
				}

				// Smooth only the attack of preFormantGain (fast release keeps stops crisp).
				double targetPreGain = frame->preFormantGain;
				if(targetPreGain > smoothPreGain) {
					smoothPreGain += (targetPreGain - smoothPreGain) * preGainAttackAlpha;
				} else {
					smoothPreGain = targetPreGain;
				}

				double voice=voiceGenerator.getNext(frame);

				double cascadeOut=cascade.getNext(frame,voiceGenerator.glottisOpen,voice*smoothPreGain);

				double fric=fricGenerator.getNext()*0.175*frame->fricationAmplitude;
				double parallelOut=parallel.getNext(frame,voiceGenerator.glottisOpen,fric*smoothPreGain);
				double out=(cascadeOut+parallelOut)*frame->outputGain;
				double filteredOut=out-lastInput+0.9995*lastOutput;
				lastInput=out;
				lastOutput=filteredOut;

				// Linear output scaling + hard clip.
				// This avoids the "clippy but not loud" compression artifacts caused by
				// soft-knee limiters on high-crest-factor signals (sharp closure spikes).
				double scaled = filteredOut * 3000.0;
				const double limit = 32767.0;
				if(scaled > limit) scaled = limit;
				if(scaled < -limit) scaled = -limit;
				sampleBuf[i].value = (int)scaled;
			} else {
				wasSilence=true;
				return i;
			}
		}
		return sampleCount;
	}

	void setFrameManager(FrameManager* frameManager) {
		this->frameManager=frameManager;
	}

};

SpeechWaveGenerator* SpeechWaveGenerator::create(int sampleRate) {return new SpeechWaveGeneratorImpl(sampleRate); }