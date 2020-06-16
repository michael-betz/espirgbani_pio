#ifndef VAL2PWM_H
#define VAL2PWM_H

// 16 bits to 8 bit CIE Lightness conversion
// Converts an 0-255 intensity value to an equivalent  0-255 LED PWM value
// from https://ledshield.wordpress.com/2012/11/13/led-brightness-to-your-eye-gamma-correction-no/
uint8_t valToPwm(int val);

#endif
