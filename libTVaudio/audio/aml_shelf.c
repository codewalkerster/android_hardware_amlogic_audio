#include<string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "aml_shelf.h"

struct IIR_param aml_IIR;

static void IIR_init(struct IIR_param *aml_iir) {
	int i;

	for (i = 0; i < COEFF_COUNT; i++) {
		aml_iir->b[i] = IIR_coefficients[0][i];
		aml_iir->a[i] = IIR_coefficients[1][i];
	}

	memset(aml_iir->cx, 0, sizeof(int) * CF  * 2);
	memset(aml_iir->cy, 0, sizeof(int) * CF  * 2);
	return;
}

static int IIR_process(int input, struct IIR_param *aml_iir, int channel) {
	int sample = input;
	int y = 0, i = 0;
	long long temp = 0;
	int cx[TMP_COEFF], cy[TMP_COEFF];

	for (i = 0; i < TMP_COEFF; i++) {
		cx[i] = aml_iir->cx[channel][i];
		cy[i] = aml_iir->cy[channel][i];
	}

	sample <<= DATA_FRACTION_BIT;
	temp = (long long)sample * (long long)(aml_iir->b[0]);
	temp += (long long)cx[0] * (long long)(aml_iir->b[1]);
	temp += (long long)cx[1] * (long long)(aml_iir->b[2]);
	temp -= (long long)cy[0] * (long long)(aml_iir->a[1]);
	temp -= (long long)cy[1] * (long long)(aml_iir->a[2]);
	temp = (temp + COEFF_HALF_ERROR) >> COEFF_FRACTION_BIT;
	y = (int)temp;
	cx[1] = cx[0];
	cx[0] = sample;
	cy[1] = cy[0];
	cy[0] = y;
	sample = y;

	for (i = 0; i < TMP_COEFF; i++) {
		aml_iir->cx[channel][i] = cx[i];
		aml_iir->cy[channel][i] = cy[i];
	}

	return ((y + DATA_HALF_ERROR)>>DATA_FRACTION_BIT);
}

int audio_IIR_process(int input, int channel) {
    return Clip(IIR_process(input, &aml_IIR, channel), -32768, 32767);
}

void audio_IIR_init(void) {
    IIR_init(&aml_IIR);
	return;
}