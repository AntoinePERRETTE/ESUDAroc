#include "sun.h"

int getDayOfYear(int d, int m, int y)
{
    int n1 = (int)((m * 275.0) / 9.0);
    int n2 = (int)((m + 9.0) / 12.0);
    int k = 1 + (int)((y - 4 * (int)(y / 4) + 2) / 3.0);

    return n1 - n2 * k + d - 30;
}

double getEquationOfTime(double j)
{
    double m = 357.0 + (0.9856 * j);

    double c = (1.914 * sin(m*RAD))
        + (0.020 * sin(2*m*RAD));

    double l = 280.0 + c + (0.9856 * j);

    double r = (-2.465 * sin(2*l*RAD))
        + (0.053 * sin(4*l*RAD));

    return ((c+r)*4.0)/60.0;
}

double getDeclination(double j)
{
    double m = 357.0 + (0.9856 * j);

    double c = (1.914 * sin(m*RAD))
        + (0.020 * sin(2*m*RAD));

    double l = 280.0 + c + (0.9856 * j);

    double sinDec = 0.3978 * sin(l*RAD);

    return asin(sinDec) * DEG;
}

/* Angle horaire pour une altitude solaire donnée */
double getHourAngle(double altitude, double decl, double lat)
{
    double cosH =
        (sin(altitude*RAD) - sin(lat*RAD)*sin(decl*RAD)) /
        (cos(lat*RAD)*cos(decl*RAD));

    return acos(cosH) * DEG / 15.0;
}

struct sunData compute_sunData()
{
    struct sunData s;

    /* Date du jour */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    s.day = t->tm_mday;
    s.month = t->tm_mon + 1;
    s.year = t->tm_year + 1900;

    s.dayOfYear = getDayOfYear(s.day,s.month,s.year);

    /* Position (exemple : Paris) */
    s.latitude = LONGITUDE;
    s.longitude = LATITUDE;

    s.declination = getDeclination(s.dayOfYear);
    s.equationOfTime = getEquationOfTime(s.dayOfYear);

    double mLon = (s.longitude * 4.0) / 60.0;

    double solarNoon = 12.0 + s.equationOfTime - mLon + UTC_OFFSET;

    /* Altitudes caractéristiques */
    double sunriseAlt = -0.833;   // lever/coucher
    double civilAlt   = -6.0;     // aube/crépuscule civil

    double Hsun = getHourAngle(sunriseAlt,s.declination,s.latitude);
    double Hcivil = getHourAngle(civilAlt,s.declination,s.latitude);

    /* Lever du soleil */
    s.sunrise = solarNoon - Hsun;
    /* Coucher du soleil */
    s.sunset  = solarNoon + Hsun;

    /* Aube */
    s.dawn = solarNoon - Hcivil;
    /* Crepuscule */
    s.dusk = solarNoon + Hcivil;

    return s;
}
