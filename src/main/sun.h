#ifndef SUN_H
#define SUN_H

#include <math.h>
#include <time.h>

#define RAD (M_PI/180.0)
#define DEG (180.0/M_PI)
#define UTC_OFFSET 1

struct sunData {
    int day, month, year;
    int dayOfYear;

    double latitude;
    double longitude;

    double declination;
    double equationOfTime;

    double sunset;

    double sunrise;

    double dawn;

    double dusk;

};

int getDayOfYear(int d, int m, int y);

double getEquationOfTime(double j);

double getDeclination(double j);

/* Angle horaire pour une altitude solaire donnée */
double getHourAngle(double altitude, double decl, double lat);

struct sunData compute_sunData();

#endif
