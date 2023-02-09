/*
    Example code compatible with the Lambda Shield 2 for Arduino and GUI frontend.
    
    Copyright (C) 2020 Bylund Automotive AB
    
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    
    Contact information of author:
    http://www.bylund-automotive.com/
    
    info@bylund-automotive.com
    
    Version history:
    2020-04-13        v1.0.0        First release.
    2020-08-30        v1.1.0        Added display support.
*/

//Define included headers.
#include <SPI.h>
#include <U8g2lib.h>

//Define CJ125 registers used.
#define           CJ125_IDENT_REG_REQUEST             0x4800        /* Identify request, gives revision of the chip. */
#define           CJ125_DIAG_REG_REQUEST              0x7800        /* Dignostic request, gives the current status. */
#define           CJ125_INIT_REG1_REQUEST             0x6C00        /* Requests the first init register. */
#define           CJ125_INIT_REG2_REQUEST             0x7E00        /* Requests the second init register. */
#define           CJ125_INIT_REG1_MODE_CALIBRATE      0x569D        /* Sets the first init register in calibration mode. */
#define           CJ125_INIT_REG1_MODE_NORMAL_V8      0x5688        /* Sets the first init register in operation mode. V=8 amplification. */
#define           CJ125_INIT_REG1_MODE_NORMAL_V17     0x5689        /* Sets the first init register in operation mode. V=17 amplification. */
#define           CJ125_DIAG_REG_STATUS_OK            0x28FF        /* The response of the diagnostic register when everything is ok. */
#define           CJ125_DIAG_REG_STATUS_NOPOWER       0x2855        /* The response of the diagnostic register when power is low. */
#define           CJ125_DIAG_REG_STATUS_NOSENSOR      0x287F        /* The response of the diagnostic register when no sensor is connected. */
#define           CJ125_INIT_REG1_STATUS_0            0x2888        /* The response of the init register when V=8 amplification is in use. */
#define           CJ125_INIT_REG1_STATUS_1            0x2889        /* The response of the init register when V=17 amplification is in use. */

//Define pin assignments.
#define           CJ125_NSS_PIN                       10            /* Pin used for chip select in SPI communication. */
#define           LED_STATUS_POWER                    7             /* Pin used for power the status LED, indicating we have power. */
#define           LED_STATUS_HEATER                   6             /* Pin used for the heater status LED, indicating heater activity. */
#define           HEATER_OUTPUT_PIN                   5             /* Pin used for the PWM output to the heater circuit. */
#define           UB_ANALOG_INPUT_PIN                 2             /* Analog input for power supply.*/
#define           UR_ANALOG_INPUT_PIN                 1             /* Analog input for temperature.*/
#define           UA_ANALOG_INPUT_PIN                 0             /* Analog input for lambda.*/

//Define adjustable parameters.       
#define           SERIAL_RATE                         10            /* Serial refresh rate in HZ (1-100)*/            
#define           UBAT_MIN                            150           /* Minimum voltage (ADC value) on Ubat to operate */
#define           hardwareId                          0x01          /* The hardwareId defines which hardware you are using. */

//Global variables.
int adcValue_UA = 0;                                                /* ADC value read from the CJ125 UA output pin */ 
int adcValue_UR = 0;                                                /* ADC value read from the CJ125 UR output pin */
int adcValue_UB = 0;                                                /* ADC value read from the voltage divider caluclating Ubat */
int adcValue_UA_Optimal = 307;                                      /* UA ADC value stored when CJ125 is in calibration mode, λ=1, this will be used ad offset respcet to nominal curve */ 
int adcValue_UR_Optimal = 0;                                        /* UR ADC value stored when CJ125 is in calibration mode, optimal temperature */
int HeaterOutput = 0;                                               /* Current PWM output value (0-255) of the heater output pin */
int CJ125_Status = 0;                                               /* Latest stored DIAG registry response from the CJ125 */
int serial_counter = 0;                                             /* Counter used to calculate refresh rate on the serial output */
int HeaterStatus = 0;                                               /* Defines the heater status for the GUI front-end */
unsigned long adcValue_UA_AVG = 0;                                  /* Average accumulator, used to smooth ADC and PID oscillation */ 
//PID regulation variables.
int dState;                                                         /* Last position input. */
int iState;                                                         /* Integrator state. */
const int iMax = 250;                                               /* Maximum allowable integrator state. */
const int iMin = -250;                                              /* Minimum allowable integrator state. */
const float pGain = 120;                                            /* Proportional gain. Default = 120*/
const float iGain = 0.8;                                            /* Integral gain. Default = 0.8*/
const float dGain = 10;                                             /* Derivative gain. Default = 10*/

//Define display.
U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

//Company logo bitmap.
PROGMEM const unsigned char Logo[] = {
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xff, 0xf8, 0x07, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x7f, 0xf8,
   0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0xf8, 0x7f, 0xfc, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x3f, 0xfc, 0x01, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x3f, 0xfe,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0xfc, 0x1f, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x7f,
   0x7f, 0x80, 0xfb, 0x07, 0xf8, 0x87, 0x3f, 0xff, 0x01, 0xe7, 0xff, 0x7f,
   0x00, 0xfc, 0x8f, 0x3f, 0xff, 0xc0, 0xf1, 0x07, 0xf8, 0xc7, 0x3f, 0xfe,
   0x81, 0xc7, 0xff, 0x7f, 0x00, 0xff, 0xc7, 0x3f, 0xff, 0xf0, 0xf8, 0x03,
   0xf8, 0xc3, 0x1f, 0xfe, 0x81, 0xc3, 0x1f, 0xff, 0x80, 0xff, 0xc3, 0x1f,
   0xfe, 0x78, 0xf8, 0x03, 0xf8, 0xe3, 0x1f, 0xff, 0xc1, 0xe3, 0x0f, 0x7f,
   0xc0, 0xff, 0xe3, 0x1f, 0xfe, 0x1c, 0xfc, 0x01, 0xfc, 0xe1, 0x8f, 0xff,
   0xc1, 0xe1, 0x8f, 0x7f, 0xc0, 0xff, 0xe1, 0x0f, 0xfe, 0x0e, 0xfc, 0x01,
   0xfc, 0xf1, 0x87, 0xff, 0xe3, 0xf1, 0x87, 0x3f, 0xe0, 0xff, 0xf1, 0x07,
   0xfe, 0x07, 0xfe, 0x00, 0xfe, 0xf0, 0xc7, 0xff, 0xe3, 0xf0, 0xc7, 0x3f,
   0x00, 0x00, 0xf0, 0x07, 0xfe, 0x03, 0xff, 0x00, 0xff, 0xf8, 0xc3, 0xf9,
   0xf3, 0xf8, 0xc3, 0x1f, 0x00, 0x00, 0xf8, 0x03, 0xfe, 0x01, 0x7f, 0x00,
   0x7f, 0xf8, 0xe3, 0xf9, 0x73, 0xfc, 0xe3, 0x0f, 0x00, 0x00, 0xfc, 0x03,
   0xfe, 0x80, 0x7f, 0x80, 0x7f, 0xfc, 0xe1, 0xf8, 0x3b, 0xfc, 0xe1, 0x0f,
   0xf0, 0x7f, 0xfc, 0x01, 0xff, 0x80, 0x3f, 0x80, 0x3f, 0xfe, 0xf1, 0xf8,
   0x3f, 0xfe, 0xf1, 0x07, 0xf8, 0x3f, 0xfe, 0x01, 0x7f, 0xc0, 0x3f, 0xc0,
   0x3f, 0xfe, 0x70, 0xf8, 0x1f, 0xfe, 0xf8, 0x07, 0xfc, 0x3f, 0xfe, 0x80,
   0x3f, 0xc0, 0x1f, 0xc0, 0x1f, 0xff, 0x38, 0xf8, 0x1f, 0xff, 0xf8, 0x03,
   0xfc, 0x1f, 0xff, 0x80, 0x3f, 0xe0, 0x1f, 0xe0, 0x0f, 0x7f, 0x38, 0xf8,
   0x0f, 0x7f, 0xfc, 0x03, 0xfe, 0xff, 0x7f, 0xc0, 0x1f, 0xe0, 0xff, 0xef,
   0xff, 0x3f, 0x1c, 0xf8, 0x8f, 0xff, 0xff, 0x01, 0xff, 0xff, 0x1f, 0xc0,
   0x1f, 0xf0, 0xff, 0xcf, 0xff, 0x1f, 0x1e, 0xf0, 0x87, 0xff, 0x7f, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0xf6, 0xff, 0x9b, 0xfb,
   0xfe, 0x9f, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c,
   0x1b, 0xb3, 0x9d, 0xdb, 0x8c, 0x59, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x3a, 0x9b, 0xf9, 0xfd, 0x6d, 0xcc, 0x59, 0x1f, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbf, 0x8d, 0xd9, 0xfa, 0x6d,
   0xc6, 0xb8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb1,
   0xc7, 0x78, 0xd0, 0x3c, 0x66, 0xb8, 0x0f, 0x00 };

//Sensor symbol bitmap.
PROGMEM const unsigned char LambdaSensorSymbol[] = {
   0xfc, 0x3f, 0xfe, 0x7f, 0x3f, 0xfc, 0x3f, 0xfc, 0x3f, 0xfc, 0x3f, 0xfc,
   0x3f, 0xfc, 0x3f, 0xfc, 0x1f, 0xf8, 0x1f, 0xf8, 0x1f, 0xf8, 0x3f, 0xfc,
   0x3f, 0xfc, 0x7f, 0xfe, 0xfe, 0x7f, 0xfc, 0x3f };

//Battery symbol bitmap.
PROGMEM const unsigned char BatterySymbol[] = {
   0xfc, 0x3f, 0xfe, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xcf, 0xf3, 0x07, 0xe0,
   0x07, 0xe0, 0x07, 0xe0, 0x07, 0xe0, 0x07, 0xe0, 0x07, 0xe0, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xfe, 0x7f, 0xfc, 0x3f };

//Heater symbol bitmap.
PROGMEM const unsigned char HeaterSymbol[] = {
   0xfc, 0x3f, 0xfe, 0x7f, 0xff, 0xff, 0xdf, 0xf6, 0x4f, 0xf2, 0x6f, 0xfb,
   0x6f, 0xfb, 0x4f, 0xf2, 0xdf, 0xf6, 0xff, 0xff, 0x07, 0xe0, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xfe, 0x7f, 0xfc, 0x3f };

//Lambda Conversion Lookup Table. (ADC 39-791).
const PROGMEM float Lambda_Conversion[753] {
  0.750, 0.751, 0.752, 0.752, 0.753, 0.754, 0.755, 0.755, 0.756, 0.757, 0.758, 0.758, 0.759, 0.760, 0.761, 0.761, 0.762, 0.763, 0.764, 0.764,
  0.765, 0.766, 0.766, 0.767, 0.768, 0.769, 0.769, 0.770, 0.771, 0.772, 0.772, 0.773, 0.774, 0.774, 0.775, 0.776, 0.777, 0.777, 0.778, 0.779,
  0.780, 0.780, 0.781, 0.782, 0.782, 0.783, 0.784, 0.785, 0.785, 0.786, 0.787, 0.787, 0.788, 0.789, 0.790, 0.790, 0.791, 0.792, 0.793, 0.793,
  0.794, 0.795, 0.796, 0.796, 0.797, 0.798, 0.799, 0.799, 0.800, 0.801, 0.802, 0.802, 0.803, 0.804, 0.805, 0.805, 0.806, 0.807, 0.808, 0.808,
  0.809, 0.810, 0.811, 0.811, 0.812, 0.813, 0.814, 0.815, 0.815, 0.816, 0.817, 0.818, 0.819, 0.820, 0.820, 0.821, 0.822, 0.823, 0.824, 0.825,
  0.825, 0.826, 0.827, 0.828, 0.829, 0.830, 0.830, 0.831, 0.832, 0.833, 0.834, 0.835, 0.836, 0.837, 0.837, 0.838, 0.839, 0.840, 0.841, 0.842,
  0.843, 0.844, 0.845, 0.846, 0.846, 0.847, 0.848, 0.849, 0.850, 0.851, 0.852, 0.853, 0.854, 0.855, 0.855, 0.856, 0.857, 0.858, 0.859, 0.860,
  0.861, 0.862, 0.863, 0.864, 0.865, 0.865, 0.866, 0.867, 0.868, 0.869, 0.870, 0.871, 0.872, 0.873, 0.874, 0.875, 0.876, 0.877, 0.878, 0.878,
  0.879, 0.880, 0.881, 0.882, 0.883, 0.884, 0.885, 0.886, 0.887, 0.888, 0.889, 0.890, 0.891, 0.892, 0.893, 0.894, 0.895, 0.896, 0.897, 0.898,
  0.899, 0.900, 0.901, 0.902, 0.903, 0.904, 0.905, 0.906, 0.907, 0.908, 0.909, 0.910, 0.911, 0.912, 0.913, 0.915, 0.916, 0.917, 0.918, 0.919,
  0.920, 0.921, 0.922, 0.923, 0.924, 0.925, 0.926, 0.927, 0.928, 0.929, 0.931, 0.932, 0.933, 0.934, 0.935, 0.936, 0.937, 0.938, 0.939, 0.940,
  0.941, 0.942, 0.944, 0.945, 0.946, 0.947, 0.948, 0.949, 0.950, 0.951, 0.952, 0.953, 0.954, 0.955, 0.957, 0.958, 0.959, 0.960, 0.961, 0.962,
  0.963, 0.965, 0.966, 0.967, 0.969, 0.970, 0.971, 0.973, 0.974, 0.976, 0.977, 0.979, 0.980, 0.982, 0.983, 0.985, 0.986, 0.987, 0.989, 0.990,
  0.991, 0.992, 0.994, 0.995, 0.996, 0.998, 0.999, 1.001, 1.003, 1.005, 1.008, 1.010, 1.012, 1.015, 1.017, 1.019, 1.022, 1.024, 1.026, 1.028,
  1.030, 1.032, 1.035, 1.037, 1.039, 1.041, 1.043, 1.045, 1.048, 1.050, 1.052, 1.055, 1.057, 1.060, 1.062, 1.064, 1.067, 1.069, 1.072, 1.075,
  1.077, 1.080, 1.082, 1.085, 1.087, 1.090, 1.092, 1.095, 1.098, 1.100, 1.102, 1.105, 1.107, 1.110, 1.112, 1.115, 1.117, 1.120, 1.122, 1.124,
  1.127, 1.129, 1.132, 1.135, 1.137, 1.140, 1.142, 1.145, 1.148, 1.151, 1.153, 1.156, 1.159, 1.162, 1.165, 1.167, 1.170, 1.173, 1.176, 1.179,
  1.182, 1.185, 1.188, 1.191, 1.194, 1.197, 1.200, 1.203, 1.206, 1.209, 1.212, 1.215, 1.218, 1.221, 1.224, 1.227, 1.230, 1.234, 1.237, 1.240,
  1.243, 1.246, 1.250, 1.253, 1.256, 1.259, 1.262, 1.266, 1.269, 1.272, 1.276, 1.279, 1.282, 1.286, 1.289, 1.292, 1.296, 1.299, 1.303, 1.306,
  1.310, 1.313, 1.317, 1.320, 1.324, 1.327, 1.331, 1.334, 1.338, 1.342, 1.345, 1.349, 1.352, 1.356, 1.360, 1.364, 1.367, 1.371, 1.375, 1.379,
  1.382, 1.386, 1.390, 1.394, 1.398, 1.401, 1.405, 1.409, 1.413, 1.417, 1.421, 1.425, 1.429, 1.433, 1.437, 1.441, 1.445, 1.449, 1.453, 1.457,
  1.462, 1.466, 1.470, 1.474, 1.478, 1.483, 1.487, 1.491, 1.495, 1.500, 1.504, 1.508, 1.513, 1.517, 1.522, 1.526, 1.531, 1.535, 1.540, 1.544,
  1.549, 1.554, 1.558, 1.563, 1.568, 1.572, 1.577, 1.582, 1.587, 1.592, 1.597, 1.601, 1.606, 1.611, 1.616, 1.621, 1.627, 1.632, 1.637, 1.642,
  1.647, 1.652, 1.658, 1.663, 1.668, 1.674, 1.679, 1.684, 1.690, 1.695, 1.701, 1.707, 1.712, 1.718, 1.724, 1.729, 1.735, 1.741, 1.747, 1.753,
  1.759, 1.764, 1.770, 1.776, 1.783, 1.789, 1.795, 1.801, 1.807, 1.813, 1.820, 1.826, 1.832, 1.839, 1.845, 1.852, 1.858, 1.865, 1.872, 1.878,
  1.885, 1.892, 1.898, 1.905, 1.912, 1.919, 1.926, 1.933, 1.940, 1.947, 1.954, 1.961, 1.968, 1.975, 1.983, 1.990, 1.997, 2.005, 2.012, 2.020,
  2.027, 2.035, 2.042, 2.050, 2.058, 2.065, 2.073, 2.081, 2.089, 2.097, 2.105, 2.113, 2.121, 2.129, 2.137, 2.145, 2.154, 2.162, 2.171, 2.179,
  2.188, 2.196, 2.205, 2.214, 2.222, 2.231, 2.240, 2.249, 2.258, 2.268, 2.277, 2.286, 2.295, 2.305, 2.314, 2.324, 2.333, 2.343, 2.353, 2.363,
  2.373, 2.383, 2.393, 2.403, 2.413, 2.424, 2.434, 2.444, 2.455, 2.466, 2.476, 2.487, 2.498, 2.509, 2.520, 2.532, 2.543, 2.554, 2.566, 2.577,
  2.589, 2.601, 2.613, 2.625, 2.637, 2.649, 2.662, 2.674, 2.687, 2.699, 2.712, 2.725, 2.738, 2.751, 2.764, 2.778, 2.791, 2.805, 2.819, 2.833,
  2.847, 2.861, 2.875, 2.890, 2.904, 2.919, 2.934, 2.949, 2.964, 2.979, 2.995, 3.010, 3.026, 3.042, 3.058, 3.074, 3.091, 3.107, 3.124, 3.141,
  3.158, 3.175, 3.192, 3.209, 3.227, 3.245, 3.263, 3.281, 3.299, 3.318, 3.337, 3.355, 3.374, 3.394, 3.413, 3.433, 3.452, 3.472, 3.492, 3.513,
  3.533, 3.554, 3.575, 3.597, 3.618, 3.640, 3.662, 3.684, 3.707, 3.730, 3.753, 3.776, 3.800, 3.824, 3.849, 3.873, 3.898, 3.924, 3.950, 3.976,
  4.002, 4.029, 4.056, 4.084, 4.112, 4.140, 4.169, 4.198, 4.228, 4.258, 4.288, 4.319, 4.350, 4.382, 4.414, 4.447, 4.480, 4.514, 4.548, 4.583,
  4.618, 4.654, 4.690, 4.726, 4.764, 4.801, 4.840, 4.879, 4.918, 4.958, 4.999, 5.040, 5.082, 5.124, 5.167, 5.211, 5.255, 5.299, 5.345, 5.391,
  5.438, 5.485, 5.533, 5.582, 5.632, 5.683 ,5.735, 5.788, 5.841, 5.896, 5.953, 6.010, 6.069, 6.129, 6.190, 6.253, 6.318, 6.384, 6.452, 6.521,
  6.592, 6.665, 6.740, 6.817, 6.896, 6.976, 7.059, 7.144, 7.231, 7.320, 7.412, 7.506, 7.602, 7.701, 7.803, 7.906, 8.013, 8.122, 8.234, 8.349,
  8.466, 8.587, 8.710, 8.837, 8.966, 9.099, 9.235, 9.374, 9.516, 9.662, 9.811, 9.963, 10.119 };

//Function for transfering SPI data to the CJ125.
uint16_t COM_SPI(uint16_t TX_data) {

  //Set chip select pin low, chip in use.
  digitalWrite(CJ125_NSS_PIN, LOW);

  //Transmit and receive.
  byte highByte = SPI.transfer(TX_data >> 8);
  byte lowByte = SPI.transfer(TX_data & 0xff);

  //Set chip select pin high, chip not in use.
  digitalWrite(CJ125_NSS_PIN, HIGH);

  //Assemble response in to a 16bit integer and return the value.
  uint16_t Response = (highByte << 8) + lowByte;
  return Response;
  
}

//Lookup Lambda Value.
float Lookup_Lambda(int Input_ADC) {
  
    //Declare and set default return value.
    float LAMBDA_VALUE = 0;

    //Validate ADC range for lookup table.
    if (Input_ADC >= 39 && Input_ADC <= 791) {
      LAMBDA_VALUE = pgm_read_float_near(Lambda_Conversion + (Input_ADC- 39 + 307 - adcValue_UA_Optimal));
    }
    
    if (Input_ADC > 791) {
      LAMBDA_VALUE = 10.119;
    }

    if (Input_ADC < 39) {
      LAMBDA_VALUE = 0.750;
    }
    
    //Return value.
    return LAMBDA_VALUE;
    
}

//Temperature regulating software (PID).
int CalculateHeaterOutput(int input) {
  
  //Calculate error term.
  int error = adcValue_UR_Optimal - input;
  
  //Set current position.
  int position = input;
  
  //Calculate proportional term.
  float pTerm = -pGain * error;
  
  //Calculate the integral state with appropriate limiting.
  iState += error;
  
  if (iState > iMax) iState = iMax;
  if (iState < iMin) iState = iMin;
  
  //Calculate the integral term.
  float iTerm = -iGain * iState;
  
  //Calculate the derivative term.
  float dTerm = -dGain * (dState - position);
  dState = position;
  
  //Calculate regulation (PI).
  int RegulationOutput = pTerm + iTerm + dTerm;
  
  //Set maximum heater output (full power).
  if (RegulationOutput > 255) RegulationOutput = 255;
  
  //Set minimum heater value (cooling).
  if (RegulationOutput < 0.0) RegulationOutput = 0;

  //Return calculated PWM output.
  return RegulationOutput;
  
}

//Function to transfer current values to front end.
void UpdateSerialOutput() {
  
  //Calculate checksum.
  uint16_t checksum = HeaterStatus + hardwareId + CJ125_Status + adcValue_UA + adcValue_UR + adcValue_UB;

  //Assembled data.
  String txString = (String)HeaterStatus;
  txString += ",";
  txString += (String)hardwareId;
  txString += ",";
  txString += (String)CJ125_Status;
  txString += ",";
  txString += (String)adcValue_UA;
  txString += ",";
  txString += (String)adcValue_UR;
  txString += ",";
  txString += (String)adcValue_UB;
  txString += ",";
  txString += (String)checksum;
  
  //Output values.
  Serial.println(txString);
  
}

//Function to read inputs and update values.
void UpdateInputs() {

  //Update CJ125 diagnostic register from SPI.
  CJ125_Status = COM_SPI(CJ125_DIAG_REG_REQUEST);

  //Update analog inputs.
  adcValue_UA = analogRead(UA_ANALOG_INPUT_PIN);
  adcValue_UR = analogRead(UR_ANALOG_INPUT_PIN);
  adcValue_UB = analogRead(UB_ANALOG_INPUT_PIN);
}

//Function to update user interfaces and output.
void UpdateUI() {
  
  //Local variables.
  const float AirFuelRatioOctane = 14.70;
  
  //Sensor is not active.
  if (HeaterStatus == 0) {
    
    //Output LED's.
    digitalWrite(LED_STATUS_POWER, LOW);  
    digitalWrite(LED_STATUS_HEATER, LOW);  

    //Output serial data.
    UpdateSerialOutput();

    //Output logo to display.
    u8g2.firstPage();
    do {
      u8g2.drawXBMP(0, 16, 128, 32, Logo);
    } while ( u8g2.nextPage() );
  }
  
  //Sensor is heating up.
  if (HeaterStatus == 1) {
     
    //Output LED's.
    digitalWrite(LED_STATUS_POWER, HIGH);  
    digitalWrite(LED_STATUS_HEATER, HIGH);  

    //Output serial data.
    UpdateSerialOutput();

    //Output display data.
    u8g2.firstPage();
    do {
      u8g2.drawXBMP(112, 4, 16, 16, LambdaSensorSymbol);
      u8g2.drawXBMP(112, 24, 16, 16, BatterySymbol);
      u8g2.drawXBMP(112, 44, 16, 16, HeaterSymbol);
    } while ( u8g2.nextPage() );
    
    //Delay for flashing LED's.
    delay(500);

    //Output display data.
    u8g2.firstPage();
    do {
      u8g2.drawXBMP(112, 4, 16, 16, LambdaSensorSymbol);
      u8g2.drawXBMP(112, 24, 16, 16, BatterySymbol);
    } while ( u8g2.nextPage() );
    
    //Output LED's.
    digitalWrite(LED_STATUS_HEATER, LOW);
    
    //Delay for flashing LED's.
    delay(500);
    
  }

  //Sensor is measuring.
  if (HeaterStatus == 2) {

    //Output LED's.
    digitalWrite(LED_STATUS_POWER, HIGH);  
    digitalWrite(LED_STATUS_HEATER, HIGH);
    
    //Output serial data.
    UpdateSerialOutput();

    //Output display data.
    u8g2.firstPage();
    do {
      u8g2.drawXBMP(112, 4, 16, 16, LambdaSensorSymbol);
      u8g2.drawXBMP(112, 24, 16, 16, BatterySymbol);
      u8g2.drawXBMP(112, 44, 16, 16, HeaterSymbol);
      u8g2.setFont(u8g2_font_helvB24_tf);
      u8g2.setCursor(0,29);
      u8g2.print(Lookup_Lambda(adcValue_UA) * AirFuelRatioOctane, 2);
      u8g2.setCursor(0,59);
      u8g2.print(Lookup_Lambda(adcValue_UA), 2);
    } while ( u8g2.nextPage() );

  }

}

//Function to set up device for operation.
void setup() {
  
  //Set up serial communication.
  Serial.begin(9600);
  
  //Set up SPI.
  SPI.begin();  /* Note, SPI will disable the bult in LED*/
  SPI.setClockDivider(SPI_CLOCK_DIV128);
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE1);
  
  //Set up digital output pins.
  pinMode(CJ125_NSS_PIN, OUTPUT);  
  pinMode(LED_STATUS_POWER, OUTPUT);
  pinMode(LED_STATUS_HEATER, OUTPUT);
  pinMode(HEATER_OUTPUT_PIN, OUTPUT);

  //Initialize display.
  u8g2.begin();
  
  //Start main function.
  start();

}

void start() {
  
  //Reset initial values.
  digitalWrite(CJ125_NSS_PIN, HIGH);
  digitalWrite(LED_STATUS_POWER, LOW);
  digitalWrite(LED_STATUS_HEATER, LOW);
  analogWrite(HEATER_OUTPUT_PIN, 0);

  //Start of operation. (Test LED's).
  digitalWrite(LED_STATUS_POWER, HIGH);
  digitalWrite(LED_STATUS_HEATER, HIGH);
  delay(200);
  digitalWrite(LED_STATUS_POWER, LOW);
  digitalWrite(LED_STATUS_HEATER, LOW);
  
  //Update heater status to off.
  HeaterStatus = 0;
  
  //Wait until everything is ready. Read CJ125 multiple times with delay in between to let it initialize. Otherwise responds OK.
  while (adcValue_UB < UBAT_MIN || CJ125_Status != CJ125_DIAG_REG_STATUS_OK) {

    //Update Values.
    UpdateInputs();

    //Update frontends.
    UpdateUI();
    
    //Delay.
    delay(100);
  }

  //Set CJ125 in calibration mode.
  COM_SPI(CJ125_INIT_REG1_MODE_CALIBRATE);

  //Let values settle.
  delay(500);

  //Store optimal values before leaving calibration mode.
  adcValue_UA_Optimal = analogRead(UA_ANALOG_INPUT_PIN);
  adcValue_UR_Optimal = analogRead(UR_ANALOG_INPUT_PIN);
  
  //Set CJ125 in normal operation mode.
  //COM_SPI(CJ125_INIT_REG1_MODE_NORMAL_V8);  /* V=0 */
  COM_SPI(CJ125_INIT_REG1_MODE_NORMAL_V17);  /* V=1 */

  /* Heat up sensor. This is described in detail in the datasheet of the LSU 4.9 sensor with a 
   * condensation phase and a ramp up face before going in to PID control. */

  //Update heater status to heating.
  HeaterStatus = 1;
  
  //Calculate supply voltage.
  float SupplyVoltage = (((float)adcValue_UB / 1023 * 5) /10000) * 110000;

  //Condensation phase, 2V for 5s.
  int CondensationPWM = (2 / SupplyVoltage) * 255;
  analogWrite(HEATER_OUTPUT_PIN, CondensationPWM);

  int t = 0;
  while (t < 5 && adcValue_UB > UBAT_MIN) {

    //Update Values.
    UpdateInputs();

    //Update frontends.
    UpdateUI();

    t += 1;
    
  }

  //Ramp up phase, +0.4V / s until 100% PWM from 8.5V.
  float UHeater = 8.5;
  while (UHeater < 13.0 && adcValue_UB > UBAT_MIN && adcValue_UR > adcValue_UR_Optimal) {

    //Update Values.
    UpdateInputs();

    //Update frontends.
    UpdateUI();
    
    //Set heater output during ramp up.
    CondensationPWM = (UHeater / SupplyVoltage) * 255;
      
    if (CondensationPWM > 255) CondensationPWM = 255; /*If supply voltage is less than 13V, maximum is 100% duty cycle*/

    analogWrite(HEATER_OUTPUT_PIN, CondensationPWM);

    //Increment Voltage.
    UHeater += 0.4;
      
  }

  //Update heater status to regulating.
  HeaterStatus = 2;
  
}

//Infinite loop.
void loop() {

  //Update Values.
  UpdateInputs();

  //Display on serial port at defined rate. Comma separate values, readable by frontends.
  if ( (100 / SERIAL_RATE) ==  serial_counter) {
    //Add to accumulator last value
    adcValue_UA_AVG=adcValue_UA_AVG+adcValue_UA;
    //Calculate the average
    adcValue_UA=adcValue_UA_AVG/(serial_counter);
    //Reset accumulator
    adcValue_UA_AVG=0;
    //Reset counter.
    serial_counter = 0;

    //Update frontends.
    UpdateUI();

  }
    else{
     //Add to accumulator last value
     adcValue_UA_AVG=adcValue_UA_AVG+adcValue_UA;
  
  }


  //Adjust PWM output by calculated PID regulation.
  if (adcValue_UR < 500 && adcValue_UB > UBAT_MIN) {
    
    //Calculate and set new heater output.
    HeaterOutput = CalculateHeaterOutput(adcValue_UR);
    analogWrite(HEATER_OUTPUT_PIN, HeaterOutput);
    
  } else {
    
    //Re-start() and wait for power.
    start();
    
  }

  //Increment serial output counter and delay for next cycle. The PID requires to be responsive but we don't need to flood the serial port, so we just update average accumulator
  serial_counter++;
  delay(10);

}
