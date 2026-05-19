#pragma once

#include "pm_weather.h"

extern PmWeatherStatus g_weather_ui;

/** 24h radial forecast rings + current conditions center; draws its own 24h rainbow rim. */
void pm_face_weather_draw(bool time_valid, int local_hour, int local_min);
