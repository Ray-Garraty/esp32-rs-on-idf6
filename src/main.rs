use core::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex};

use log::info;

use esp_idf_hal::gpio::Pull;
use esp_idf_hal::peripherals::Peripherals;

use ecotiter_fw::errors::StepperError;
use ecotiter_fw::limitswitch::LimitSwitch;
use ecotiter_fw::stepper::rmt_stepper::RmtStepper;
use ecotiter_fw::types::Direction;
use ecotiter_fw::webserver::WebServer;
use ecotiter_fw::wifi::WifiManager;

static LIMIT_FULL_TRIGGERED: AtomicBool = AtomicBool::new(false);
static LIMIT_EMPTY_TRIGGERED: AtomicBool = AtomicBool::new(false);

fn main() {
    esp_idf_sys::link_patches();
    ecotiter_fw::logger::init();

    unsafe {
        esp_idf_sys::esp_task_wdt_deinit();
    }

    info!("=== EcoTiter firmware ===");

    let peripherals = Peripherals::take().expect("Peripherals::take()");

    let mut stepper = RmtStepper::new(
        peripherals.pins.gpio25.degrade_output(),
        peripherals.pins.gpio26.degrade_output(),
        peripherals.pins.gpio27.degrade_output(),
    )
    .expect("RmtStepper::new()");

    stepper.set_stop_flag(&LIMIT_FULL_TRIGGERED);

    let mut limit_full = LimitSwitch::new(
        peripherals.pins.gpio32,
        Pull::Down,
        &LIMIT_FULL_TRIGGERED,
    )
    .expect("LimitSwitch FULL (GPIO32)");

    let mut limit_empty = LimitSwitch::new(
        peripherals.pins.gpio35,
        Pull::Floating,
        &LIMIT_EMPTY_TRIGGERED,
    )
    .expect("LimitSwitch EMPTY (GPIO35)");

    let mut led = esp_idf_hal::gpio::PinDriver::output(
        peripherals.pins.gpio2.degrade_output(),
    )
    .expect("LED GPIO2");

    led.set_low().ok();

    {
        use esp_idf_svc::eventloop::EspSystemEventLoop;
        use esp_idf_svc::nvs::EspDefaultNvsPartition;

        let sys_loop = EspSystemEventLoop::take().expect("EspSystemEventLoop::take()");
        let nvs = EspDefaultNvsPartition::take().expect("EspDefaultNvsPartition::take()");

        let ble_active = Arc::new(AtomicBool::new(false));

        let wifi_mgr: Arc<Mutex<WifiManager>> = Arc::new(Mutex::new(
            WifiManager::new(
                peripherals.modem,
                sys_loop,
                Some(nvs),
                ble_active.clone(),
            )
            .expect("WifiManager::new()"),
        ));

        wifi_mgr.lock().unwrap().init();

        let webserver =
            WebServer::new(wifi_mgr.clone());

        // Ensure WiFi is started before loop
        std::thread::sleep(std::time::Duration::from_millis(200));

        stepper.enable().expect("enable");
        stepper
            .set_direction(Direction::Cw)
            .expect("set_direction");

        info!("Stepper 500 Hz CW — LED=ON when GPIO32=HIGH");

        let chunk: Vec<u32> = vec![2000; 64];
        let mut motor_stopped = false;

        loop {
            // WiFi processing (low priority)
            if let Ok(mut wifi) = wifi_mgr.lock() {
                let ble = ble_active.load(std::sync::atomic::Ordering::Relaxed);
                wifi.set_ble_active(ble);
                wifi.process();
            }

            // Restart if WiFi credentials updated via captive portal
            if webserver.restart_pending() {
                info!("Restart pending after WiFi config — restarting");
                std::thread::sleep(std::time::Duration::from_millis(500));
                unsafe {
                    esp_idf_sys::esp_restart();
                }
            }

            // Stepper + limit switch loop (existing logic)
            let full_active = limit_full.level();

            if full_active {
                led.set_high().ok();
            } else {
                led.set_low().ok();
            }

            if !motor_stopped {
                if full_active {
                    stepper.emergency_stop().ok();
                    info!("FULL switch active — motor stopped, LED ON");
                    motor_stopped = true;
                } else {
                    match stepper.move_steps(&chunk) {
                        Ok(()) => {}
                        Err(StepperError::LimitSwitchReached) => {
                            stepper.emergency_stop().ok();
                            info!("FULL switch hit during chunk — motor stopped");
                            motor_stopped = true;
                        }
                        Err(e) => {
                            info!("Stepper error: {:?}", e);
                            motor_stopped = true;
                        }
                    }
                }
            }

            if limit_empty.is_triggered() {
                info!("EMPTY switch GPIO35 HIGH (latched)");
                limit_empty.clear();
                limit_empty.rearm().ok();
            }

            if motor_stopped && limit_full.is_triggered() {
                limit_full.clear();
                limit_full.rearm().ok();
            }

            std::thread::sleep(std::time::Duration::from_millis(10));
        }
    }
}
