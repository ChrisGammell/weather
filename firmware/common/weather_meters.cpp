#include <alogging/alogging.h>

#include "weather_meters.h"
#include "debug.h"
#include "rtc.h"

namespace fk {

static WeatherMeters *global_weather_meters{ nullptr };

void isr_wind();
void isr_rain();

void isr_wind_falling() {
    global_weather_meters->wind();
}

void isr_rain_falling() {
    Serial.print("*");
}

void isr_rain_change() {
    auto value = digitalRead(6);
    Serial.println(value);
    if (value) {
        global_weather_meters->rain();
    }
}

WeatherMeters::WeatherMeters(Watchdog &watchdog, FlashState<WeatherState> &flash) : flash_(&flash) {
    global_weather_meters = this;
}

void WeatherMeters::wind() {
    auto now = millis();
    if (now - lastWindAt > 10) {
        windTicks++;
        lastWindAt = now;
    }
}

void WeatherMeters::rain() {
    auto now = millis();
    if (now - lastRainAt > 10) {
        auto minute = flash_->state().date_time().minute();
        flash_->state().lastHourOfRain[minute] += RainPerTick;
        lastRainAt = now;
    }
}

bool WeatherMeters::setup() {
    pinMode(PinWindDir, INPUT);
    pinMode(PinWindSpeed, INPUT_PULLUP);
    pinMode(PinRain, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(PinWindSpeed), isr_wind_falling, FALLING);
    attachInterrupt(digitalPinToInterrupt(PinRain), isr_rain_falling, FALLING);
    attachInterrupt(digitalPinToInterrupt(PinRain), isr_rain_change, CHANGE);

    return true;
}

float WeatherMeters::getHourlyRain() {
    auto total = 0.0f;
    for (auto i = 0; i < 60; ++i) {
        total += flash_->state().lastHourOfRain[i];
    }
    return total;
}

WindReading WeatherMeters::getWindReading() {
    auto speed = getWindSpeed();
    auto direction = getWindDirection();
    return WindReading{ speed, direction };
}

WindReading WeatherMeters::getTwoMinuteWindAverage() {
    auto &persistedState = flash_->state();
    auto speedSum = 0.0f;
    auto directionSum = persistedState.lastTwoMinutesOfWind[0].direction.angle;
    auto d = persistedState.lastTwoMinutesOfWind[0].direction.angle;
    auto numberOfSamples = 0;
    for (auto i = 1 ; i < 120; i++) {
        if (persistedState.lastTwoMinutesOfWind[i].direction.angle != -1) {
            auto delta = persistedState.lastTwoMinutesOfWind[i].direction.angle - d;

            if (delta < -180) {
                d += delta + 360;
            }
            else if (delta > 180) {
                d += delta - 360;
            }
            else {
                d += delta;
            }

            directionSum += d;

            speedSum += persistedState.lastTwoMinutesOfWind[i].speed;

            numberOfSamples++;
        }
    }

    auto averageSpeed =  speedSum / (float)numberOfSamples;

    auto averageDirection = (int16_t)(directionSum / numberOfSamples);
    if (averageDirection >= 360) {
        averageDirection -= 360;
    }
    if (averageDirection < 0) {
        averageDirection += 360;
    }

    return WindReading{ averageSpeed, WindDirection{ -1, averageDirection } };
}

WindReading WeatherMeters::getLargestRecentWindGust() {
    auto gust = WindReading{};
    for (auto i = 0; i < 10; ++i) {
        if (flash_->state().windGusts[i].strongerThan(gust)) {
            gust = flash_->state().windGusts[i];
        }
    }
    return gust;
}

bool WeatherMeters::tick() {
    if (millis() - lastStatusTick < 500) {
        return false;
    }

    auto &persistedState = flash_->state();
    auto now = clock.now();

    // Is persisted state more than a few minutes from us?
    auto nowUnix = now.unixtime();
    auto savedUnix = persistedState.time;
    auto difference = nowUnix > savedUnix ? nowUnix - savedUnix : savedUnix - nowUnix; // Avoid overflow.
    if (difference > 60 * 5) {
        loginfof("Meters", "Zeroing persisted state! (%lu - %lu = %lu)", nowUnix, savedUnix, difference);
        persistedState.clear();
    }

    if (now.second() != persistedState.date_time().second()) {
        lastStatusTick = millis();

        currentWind = getWindReading();

        if (++persistedState.twoMinuteSecondsCounter > 119) {
            persistedState.twoMinuteSecondsCounter = 0;
        }

        if (now.minute() != persistedState.date_time().minute()) {
            FormattedTime nowFormatted{ now };
            loginfof("Meters", "New Minute: %s", nowFormatted.toString());

            if (now.hour() != persistedState.date_time().hour()) {
                loginfof("Meters", "New Hour");

                persistedState.previousHourlyRain = getHourlyRain();
                for (auto i = 0; i < 60; ++i) {
                    persistedState.lastHourOfRain[i] = 0;
                }

                persistedState.hourlyWindGust.zero();
                if (now.hour() < persistedState.date_time().hour()) {
                    loginfof("Meters", "New Day (%d %d)", now.hour(), persistedState.date_time().hour());
                }
            }

            persistedState.lastHourOfRain[now.minute()] = 0;

            if (++persistedState.tenMinuteMinuteCounter > 9) {
                persistedState.tenMinuteMinuteCounter = 0;
            }

            persistedState.windGusts[persistedState.tenMinuteMinuteCounter].zero();
        }

        persistedState.lastTwoMinutesOfWind[persistedState.twoMinuteSecondsCounter] = currentWind;

        if (currentWind.strongerThan(persistedState.windGusts[persistedState.tenMinuteMinuteCounter])) {
            persistedState.windGusts[persistedState.tenMinuteMinuteCounter] = currentWind;
        }

        if (currentWind.strongerThan(persistedState.hourlyWindGust)) {
            persistedState.hourlyWindGust = currentWind;
        }

        persistedState.time = now.unixtime();

        if (millis() - lastSave > 10000) {
            save();
            lastSave = millis();
        }

        return true;
    }

    return false;
}

float WeatherMeters::getWindSpeed() {
    auto delta = (millis() - lastWindCheck) / 1000.0;
    auto speed = windTicks / (float)delta;

    windTicks = 0;
    lastWindCheck = millis();

    return speed * WindPerTick;
}

WindDirection WeatherMeters::getWindDirection() {
    auto adc = (int16_t)analogRead(PinWindDir);
    auto direction = WindDirection {
        adc, windAdcToAngle(adc)
    };
    return direction;
}

void WeatherMeters::save() {
    flash_->save();

    auto savedUnix = flash_->state().time;
    loginfof("Meters", "Save (hourly rain: %f) (%lu) (ts = %lu)", getHourlyRain(), savedUnix, flash_->state().link.header.timestamp);
}

int16_t WeatherMeters::windAdcToAngle(int16_t adc) {
    if (adc < 380) return 113;
    if (adc < 393) return 68;
    if (adc < 414) return 90;
    if (adc < 456) return 158;
    if (adc < 508) return 135;
    if (adc < 551) return 203;
    if (adc < 615) return 180;
    if (adc < 680) return 23;
    if (adc < 746) return 45;
    if (adc < 801) return 248;
    if (adc < 833) return 225;
    if (adc < 878) return 338;
    if (adc < 913) return 0;
    if (adc < 940) return 293;
    if (adc < 967) return 315;
    if (adc < 990) return 270;
    return -1;
}

}

