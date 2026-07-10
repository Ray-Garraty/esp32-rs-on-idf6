#include <cstdint>
#include <expected>

#include "domain/calibration.hpp"
#include "domain/errors.hpp"

namespace ecotiter::infrastructure::storage {

domain::Result<domain::CalibrationData, domain::ResourceError> calibrationRead() {
    domain::CalibrationData cal{};
    cal.stepsPerMl = domain::CalibrationData::kDefaultStepsPerMl;
    cal.nominalVolumeMl = domain::CalibrationData::kDefaultNominalVolumeMl;
    cal.speedCoeff = domain::CalibrationData::kDefaultSpeedCoeff;
    cal.minFreqHz = domain::CalibrationData::kDefaultMinFreqHz;
    cal.maxFreqHz = domain::CalibrationData::kDefaultMaxFreqHz;
    return cal;
}

domain::Result<void, domain::ResourceError> calibrationWrite(const domain::CalibrationData&) {
    return {};
}

static uint16_t s_stubAX1000 = 1000;
static int16_t s_stubB = 0;

void adcCalibrationRead(uint16_t& aX1000, int16_t& b) {
    aX1000 = s_stubAX1000;
    b = s_stubB;
}

domain::Result<void, domain::ResourceError> adcCalibrationWrite(uint16_t aX1000, int16_t b) {
    s_stubAX1000 = aX1000;
    s_stubB = b;
    return {};
}

} // namespace ecotiter::infrastructure::storage
