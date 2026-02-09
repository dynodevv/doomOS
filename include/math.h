/*
 * math.h — Freestanding stub for doomOS
 *
 * Provides basic math function declarations.  The implementations
 * use x87 FPU or GCC builtins.
 */

#ifndef _MATH_H
#define _MATH_H

double floor(double x);
double ceil(double x);
double sqrt(double x);
double fabs(double x);
double sin(double x);
double cos(double x);
double atan2(double y, double x);
double pow(double base, double exp);
double log(double x);
double fmod(double x, double y);
double ldexp(double x, int exp);
float  fabsf(float x);

#endif /* _MATH_H */
