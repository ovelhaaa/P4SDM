/*
 * LowPassFilter.h
 *
 * Copyright 2012 Tim Barrass
 *
 * This file is part of Mozzi.
 *
 * Mozzi is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 *
 */

#ifndef LOWPASS_H_
#define LOWPASS_H_
#include <stdint.h>

/*
simple resonant filter posted to musicdsp.org by Paul Kellett http://www.musicdsp.org/archive.php?classid=3#259

// set feedback amount given f and q between 0 and 1
fb = q + q/(1.0 - f);

// for each sample...
buf0 = buf0 + f * (in - buf0 + fb * (buf0 - buf1));
buf1 = buf1 + f * (buf0 - buf1);
out = buf1;

fixed point version of the filter
"dave's blog of art and programming" http://www.pawfal.org/dave/blog/2011/09/
*/


// we are using .n fixed point (n bits for the fractional part)
#define FX_SHIFT 8
#define SHIFTED_1 ((uint8_t) 255)

/** A resonant low pass filter for audio signals.
*/
class LowPassFilter
{

public:


	/** Constructor.
	*/
	LowPassFilter() : q(0), f(0), fb(0), buf0(0), buf1(0) {
	}

	// Control context: clear history without changing filter coefficients.
	void reset() { buf0 = buf1 = 0; }


	/** Set the cut off frequency,
	@param cutoff use the range 0-255 to represent 0-8192 Hz (AUDIO_RATE/2).
	Be careful of distortion at the lower end, especially with high resonance.
	*/
	void setCutoffFreq(uint8_t cutoff)
	{
		f = cutoff;
		refresh();
	}


	/** Set the resonance. If you hear unwanted distortion, back off the resonance.
	@param resonance in the range 0-255, with 255 being most resonant.
	*/
	void setResonance(uint8_t resonance)
	{
		q = resonance;
		refresh();
	}

	/** Calculate the next sample, given an input signal.
	@param in the signal input.
	@return the signal output.
	@note Timing: about 11us.
	*/
	//	10.5 to 12.5 us, mostly 10.5 us (was 14us)
#if P4SDM_APP
    inline __attribute__((always_inline))
#else
    inline
#endif
	int next(int in)
	{
		//setPin13High();
		buf0+=fxmul(((in - buf0) + fxmul(fb, buf0-buf1)), f);
#if P4SDM_APP
        // Bound history before downstream int32 volume/pan multiplication.
        // Intended source/master input and q<=255 keep intermediate products
        // inside int32; this guard handles excessive resonant overshoot.
        // Master default can receive the sum of sixteen voices; preserve the
        // accepted open coefficients and exact unrestricted default output.
        if (f != 255)
          buf0 = buf0 < -65535 ? -65535 : buf0 > 65535 ? 65535 : buf0;
#endif
        buf1+=ifxmul(buf0-buf1, f); // could overflow if input changes fast
		//setPin13Low();
		return buf1;
	}


private:
	void refresh() { fb = q + ucfxmul(q, SHIFTED_1 - f); }
	uint8_t q;
	uint8_t f;
	unsigned int fb;
	int buf0,buf1;


	// // multiply two fixed point numbers (returns fixed point)
	// inline
	// long fxmul(long a, long b)
	// {
	// 	return (a*b)>>FX_SHIFT;
	// }

	// multiply two fixed point numbers (returns fixed point)
	inline
	unsigned int ucfxmul(uint8_t a, uint8_t b)
	{
		return (((unsigned int)a*b)>>FX_SHIFT);
	}
	
		// multiply two fixed point numbers (returns fixed point)
	inline
	int ifxmul(int a, uint8_t b)
	{
		return ((a*b)>>FX_SHIFT);
	}
	
	// multiply two fixed point numbers (returns fixed point)
	inline
	long fxmul(long a, int b)
	{
		return ((a*b)>>FX_SHIFT);
	}

};

/**
@example 10.Audio_Filters/LowPassFilter/LowPassFilter.ino
This example demonstrates the LowPassFilter class.
*/

#endif /* LOWPASS_H_ */
