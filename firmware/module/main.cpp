#include <Arduino.h>
#include <Wire.h>

#include <fk-module.h>

#include "readings.h"

namespace fk {

class WeatherModule : public Module {
private:
    TwoWireBus bus{ Wire4and3 };
    Delay delay{ 500 };
    WeatherReadings *weatherReadings;

public:
    WeatherModule(ModuleInfo &info, WeatherReadings &weatherReadings);

public:
    ModuleReadingStatus beginReading(PendingSensorReading &pending) override;
    ModuleReadingStatus readingStatus(PendingSensorReading &pending) override;
    void done(Task &task) override;

};

WeatherModule::WeatherModule(ModuleInfo &info, WeatherReadings &weatherReadings) :
    Module(bus, info), weatherReadings(&weatherReadings) {
}

ModuleReadingStatus WeatherModule::beginReading(PendingSensorReading &pending) {
    weatherReadings->begin(pending);
    push(delay); // This is to give us time to reply with the backoff. Should be avoidable?
    push(*weatherReadings);

    return ModuleReadingStatus{ 5000 };
}

ModuleReadingStatus WeatherModule::readingStatus(PendingSensorReading &pending) {
    return ModuleReadingStatus{};
}

void WeatherModule::done(Task &task) {
    resume();
}

}

extern "C" {

void setup() {
    Serial.begin(115200);

    while (!Serial && millis() < 2000) {
        delay(100);
    }

    debugfpln("Module", "Starting (%lu free)", fk_free_memory());

    fk::ModuleInfo info = {
        8,
        18,
        "Weather",
        {
            {"Temp #1", "°C",},
            {"Humidity", "%",},
            {"Temp #2", "°C",},
            {"Pressure", "pa",},
            {"Altitude", "m",},
            {"Ambient IR", "",},
            {"Ambient Visible", "",},
            {"Ambient Lux", "",},

            {"Wind Speed", "km/hr"},
            {"Wind Dir", ""},
            {"Hr Wind Speed", "km/hr"},
            {"Hr Wind Dir", ""},
            {"10m Wind Gust", "km/hr"},
            {"10m Wind Dir", ""},
            {"2m Wind Gust", "km/hr"},
            {"2m Wind Dir", ""},

            {"Prev Hrly Rain", "mm"},
            {"Hourly Rain", "mm"},
        },
        {
            {}, {}, {}, {}, {}, {}, {}, {},
            {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
        },
    };

    fk::ModuleHardware hardware;
    fk_assert(hardware.serialFlash.begin(fk::ModuleHardware::PIN_FLASH_CS));

    fk::WeatherMeters meters(hardware.serialFlash);
    fk::WeatherReadings weatherReadings(hardware, meters);
    fk::WeatherModule module(info, weatherReadings);

    weatherReadings.setup();

    module.begin();

    while (true) {
        module.tick();
        meters.tick();

        delay(10);
    }
}

void loop() {
}

}
