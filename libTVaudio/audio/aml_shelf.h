#ifndef AML_SHELF_H
#define AML_SHELF_H

#define   COEFF_FRACTION_BIT    24
#define   DATA_FRACTION_BIT     10
#define   COEFF_HALF_ERROR      ((0x1) << (COEFF_FRACTION_BIT - 1))
#define   DATA_HALF_ERROR       ((0x1) << (DATA_FRACTION_BIT - 1))

/* Count if coefficients */
#define   COEFF_COUNT       3
#define   TMP_COEFF         2
/*Channel Count*/
#define   CF                2

#define   Clip(acc,min,max) ((acc) > max ? max : ((acc) < min ? min : (acc)))

struct IIR_param {
    /*B coefficient array*/
    int   b[COEFF_COUNT];
    /*A coefficient array*/
    int   a[COEFF_COUNT];
    /*Circular buffer for channel input data*/
    int   cx[CF][TMP_COEFF];
    /*Circular buffer for channel output data*/
    int   cy[CF][TMP_COEFF];
};

static int IIR_coefficients[2][COEFF_COUNT] = {
       {/*B coefficients*/
                36224809,    16150517,     1797672,
       },
       {/*A coefficients*/
                16777216,    26740673,    10655109,
       },

};

int audio_IIR_process(int input, int channel);
void audio_IIR_init(void);

#endif

