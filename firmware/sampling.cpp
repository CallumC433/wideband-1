#include "sampling.h"

#include "ch.h"
#include "hal.h"

#include "wideband_config.h"

#include "analog_input.h"

// Stored results
float nernstAc = 0;
float nernstDc = 0;
volatile float pumpCurrentSenseVoltage = 0;

constexpr float f_abs(float x)
{
    return x > 0 ? x : -x;
}

static THD_WORKING_AREA(waSamplingThread, 256);

static void SamplingThread(void*)
{
    float r_2 = 0;
    float r_3 = 0;

    while(true)
    {
        // First toggle the pin
        palTogglePad(GPIOB, 8);
        auto result = AnalogSample();

        float r_1 = result.NernstVoltage;

        // r2_opposite_phase estimates where the previous sample would be had we not been toggling
        // AKA the absolute value of the difference between r2_opposite_phase and r2 is the amplitude
        // of the AC component on the nernst voltage.  We have to pull this trick so as to use the past 3
        // samples to cancel out any slope in the DC (aka actual nernst cell output) from the AC measurement
        // See firmware/sampling.png for a drawing of what's going on here
        float r2_opposite_phase = (r_1 + r_3) / 2;

        // Compute AC (difference) and DC (average) components
        nernstAc = f_abs(r2_opposite_phase - r_2);
        nernstDc = (r2_opposite_phase + r_2) / 2;

        pumpCurrentSenseVoltage = 0.8f * pumpCurrentSenseVoltage + 0.2f * (result.PumpCurrentVoltage - HALF_VCC);

        // Shift history over by one
        r_3 = r_2;
        r_2 = r_1;
    }
}

void StartSampling()
{
    adcStart(&ADCD1, nullptr);
    chThdCreateStatic(waSamplingThread, sizeof(waSamplingThread), NORMALPRIO + 5, SamplingThread, nullptr);
}

float GetNernstAc()
{
    return nernstAc;
}

float GetSensorInternalResistance()
{
    // Sensor is the lowside of a divider, top side is 22k, and 3.3v AC pk-pk is injected
    return ESR_SUPPLY_R / (VCC_VOLTS / GetNernstAc() - 1);
}

float GetNernstDc()
{
    return nernstDc;
}

float GetPumpNominalCurrent()
{
    // Gain is 10x, then a 61.9 ohm resistor
    // Effective resistance with the gain is 619 ohms
    // 1000 is to convert to milliamperes
    constexpr float ratio = -1000 / (PUMP_CURRENT_SENSE_GAIN * LSU_SENSE_R);
    return pumpCurrentSenseVoltage * ratio;
}