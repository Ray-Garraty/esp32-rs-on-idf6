#include "infrastructure/motor_task.hpp"
#include "infrastructure/drivers/tmc_uart.hpp"
#include "infrastructure/cal_cache.hpp"
#include "domain/calibration.hpp"

// Non-null sentinel — xQueueSend stub always returns pdTRUE
static int s_queueSentinel;
QueueHandle_t ecotiter::infrastructure::gMotorCmdQueue = &s_queueSentinel;
QueueHandle_t ecotiter::infrastructure::gSmResultQueue = &s_queueSentinel;
ecotiter::infrastructure::drivers::TmcUart ecotiter::infrastructure::gTmcUart;

// Calibration cache for host tests — matches default values from nvs.cpp:259
static ecotiter::domain::CalibrationData s_testCal{
    ecotiter::domain::CalibrationData::kDefaultStepsPerMl,
    ecotiter::domain::CalibrationData::kDefaultNominalVolumeMl,
    ecotiter::domain::CalibrationData::kDefaultSpeedCoeff,
    ecotiter::domain::CalibrationData::kDefaultMinFreqHz,
    ecotiter::domain::CalibrationData::kDefaultMaxFreqHz,
};
struct CalCacheInit {
    CalCacheInit() {
        ecotiter::infrastructure::gCalCache.store(&s_testCal, std::memory_order_release);
    }
};
static CalCacheInit s_calInit;
