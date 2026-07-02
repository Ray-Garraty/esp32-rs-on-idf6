//! Dedicated motor thread for blocking RMT stepper control.
//!
//! This module is entirely gated behind `#[cfg(target_arch = "xtensa")]`
//! because it imports `RmtStepper`, `PinDriver`, and other xtensa-only
//! infrastructure types.
//!
//! # Architecture
//!
//! The motor task owns the `RmtStepper` exclusively. It reads
//! `CommandWithId` from `cmd_rx`, executes the motion via
//! `RmtStepper::move_steps()`, updates atomic globals, sends
//! `StatusUpdate` on `status_tx`, and sends the completion response
//! on `response_tx`.
//!
//! # Golden Rule
//!
//! This is the ONLY place where blocking RMT calls are allowed.
//! The main loop must NEVER call `move_steps` or `move_steps_intervals`.

#![cfg(target_arch = "xtensa")]
#![forbid(unsafe_code)]

use std::sync::mpsc;
use std::time::Duration;

use log::info;

use crate::application::command::CommandResponse;
use crate::config;
use crate::domain::burette::{BuretteCommand, BuretteOperation, BuretteState, RinsePhase};
use crate::domain::calibration::{self, CalibrationConfig};
use crate::domain::channels::{CommandWithId, StatusUpdate};
use crate::domain::context::MotorContext;
use crate::domain::driver_traits::StepperMotor;
use crate::domain::motor_state;
use crate::domain::types::{Direction, Hz, Ml, Steps};
use crate::errors::StepperError;
use crate::infrastructure::drivers::stepper::RmtStepper;

/// Spawn the motor task thread.
///
/// Takes ownership of `stepper`, `cmd_rx`, `status_tx`, and `response_tx`.
/// The thread has an 8 KB stack and is named "motor".
///
/// # Parameters
///
/// * `stepper` — Initialised `RmtStepper` (homing already completed).
/// * `channels` — Static `SystemChannels` from which cmd_rx and status_tx are extracted.
/// * `cal_config` — Static calibration configuration.
/// * `response_tx` — Channel to send command completion responses to main loop.
pub fn spawn(
    stepper: RmtStepper<'static>,
    channels: &'static crate::domain::channels::SystemChannels,
    cal_config: &'static CalibrationConfig,
    response_tx: mpsc::SyncSender<(u64, CommandResponse)>,
) {
    // Extract the fields we need from SystemChannels.
    // We cannot pass &SystemChannels to the thread because Receiver fields are !Sync.
    // SAFETY: cmd_rx is 'static, sole consumer is this thread.
    let cmd_rx: &'static std::sync::Mutex<mpsc::Receiver<CommandWithId>> = &channels.cmd_rx;
    let status_tx = channels.status_tx.clone();
    let _ = std::thread::Builder::new()
        .stack_size(config::MOTOR_THREAD_STACK)
        .name("motor".into())
        .spawn(move || {
            run(stepper, cmd_rx, &status_tx, cal_config, &response_tx);
        });
}

/// Main loop of the motor thread.
///
/// Owns the `RmtStepper`. Reads `CommandWithId` from `cmd_rx`, executes motion,
/// updates atomics, and sends responses.
fn run(
    mut stepper: RmtStepper<'static>,
    cmd_rx: &'static std::sync::Mutex<mpsc::Receiver<CommandWithId>>,
    status_tx: &mpsc::Sender<StatusUpdate>,
    cal_config: &'static CalibrationConfig,
    response_tx: &mpsc::SyncSender<(u64, CommandResponse)>,
) {
    let ctx = MotorContext;
    let mut iteration: u64 = 0;

    loop {
        iteration += 1;

        // Try to receive a command (non-blocking).
        // Handle the lock error (PoisonError) first so the MutexGuard is
        // dropped immediately, satisfying significant_drop_tightening.
        let cmd = match cmd_rx.lock() {
            Ok(g) => g.try_recv(),
            Err(_poisoned) => {
                log::error!("Motor: cmd_rx mutex poisoned");
                break;
            }
        };
        match cmd {
            Ok(wrapped) => {
                motor_state::MOTOR_BUSY.store(true, std::sync::atomic::Ordering::Release);
                let cmd_id = wrapped.id;
                handle_command(
                    &mut stepper,
                    &wrapped.cmd,
                    cmd_id,
                    cal_config,
                    status_tx,
                    response_tx,
                    &ctx,
                );
                motor_state::MOTOR_BUSY.store(false, std::sync::atomic::Ordering::Release);
            }
            Err(mpsc::TryRecvError::Empty) => {
                // Idle: sleep to avoid busy-waiting
                std::thread::sleep(Duration::from_millis(config::MOTOR_IDLE_SLEEP_MS));
            }
            Err(mpsc::TryRecvError::Disconnected) => {
                log::error!("Motor: cmd_rx disconnected, exiting");
                break;
            }
        }

        // Log stack watermark every 100 iterations (~1 second at 10ms sleep)
        //
        // # Lint justification
        //
        // `manual_is_multiple_of`: `iteration % 100 == 0` is intentionally
        // every-100-iteration check; 100 is a positive constant, so the
        // result is always non-negative and the expression is correct.
        #[allow(clippy::manual_is_multiple_of)]
        if iteration % 100 == 0 {
            let wm = crate::esp_safe::stack_watermark();
            info!("Motor stack watermark: {wm} bytes free");
            if wm < 2048 {
                log::error!(
                    "Motor stack critically low ({wm} bytes) — increase MOTOR_THREAD_STACK"
                );
            }
        }
    }
}

/// Dispatch a `BuretteCommand` to the appropriate stepper operation.
///
/// # Lint justification
///
/// `too_many_lines`: Linear state-machine dispatcher with one arm per
/// `BuretteCommand` variant (Fill, Empty, Dose, Rinse, Stop, EmergencyStop,
/// Reset). Each arm is self-contained — parameter extraction, validation,
/// status update. Splitting into 7 separate functions with identical
/// parameter lists would reduce readability. Length reflects the number of
/// variants, not complexity.
#[allow(clippy::too_many_lines)]
fn handle_command(
    stepper: &mut RmtStepper<'static>,
    cmd: &BuretteCommand,
    cmd_id: u64,
    cal_config: &CalibrationConfig,
    status_tx: &mpsc::Sender<StatusUpdate>,
    response_tx: &mpsc::SyncSender<(u64, CommandResponse)>,
    ctx: &MotorContext,
) {
    let operation = cmd_operation(cmd);

    match cmd {
        BuretteCommand::Fill { speed } => {
            let steps =
                calibration::volume_to_steps(Ml(cal_config.nominal_vol), cal_config.steps_per_ml);
            let freq = u32::from(calibration::speed_to_frequency(
                *speed,
                cal_config.speed_coeff,
                cal_config.min_freq,
                cal_config.max_freq,
            ));
            stepper.set_direction(Direction::LiqIn);
            execute_motion(
                stepper,
                Steps(steps.0),
                Hz(freq),
                ctx,
                cal_config,
                status_tx,
                cmd_id,
                response_tx,
                &BuretteState::Filling {
                    target_ml: Ml(cal_config.nominal_vol),
                },
                operation,
            );
        }
        BuretteCommand::Empty { speed } => {
            let steps =
                calibration::volume_to_steps(Ml(cal_config.nominal_vol), cal_config.steps_per_ml);
            let freq = u32::from(calibration::speed_to_frequency(
                *speed,
                cal_config.speed_coeff,
                cal_config.min_freq,
                cal_config.max_freq,
            ));
            stepper.set_direction(Direction::LiqOut);
            execute_motion(
                stepper,
                Steps(-(steps.0.abs())),
                Hz(freq),
                ctx,
                cal_config,
                status_tx,
                cmd_id,
                response_tx,
                &BuretteState::Emptying {
                    target_ml: Ml(cal_config.nominal_vol),
                },
                operation,
            );
        }
        BuretteCommand::Dose { volume, speed } => {
            let steps = calibration::volume_to_steps(*volume, cal_config.steps_per_ml);
            let freq = u32::from(calibration::speed_to_frequency(
                *speed,
                cal_config.speed_coeff,
                cal_config.min_freq,
                cal_config.max_freq,
            ));
            stepper.set_direction(Direction::LiqOut);
            execute_motion(
                stepper,
                Steps(-(steps.0.abs())),
                Hz(freq),
                ctx,
                cal_config,
                status_tx,
                cmd_id,
                response_tx,
                &BuretteState::Dosing {
                    remaining_ml: *volume,
                },
                operation,
            );
        }
        BuretteCommand::Rinse { cycles, speed } => {
            let steps =
                calibration::volume_to_steps(Ml(cal_config.nominal_vol), cal_config.steps_per_ml);
            let freq = u32::from(calibration::speed_to_frequency(
                *speed,
                cal_config.speed_coeff,
                cal_config.min_freq,
                cal_config.max_freq,
            ));
            let mut last_err: Option<StepperError> = None;
            for cycle in 0..*cycles {
                // Fill phase
                stepper.set_direction(Direction::LiqIn);
                motor_state::set_burette_state_tag(&BuretteState::Rinsing {
                    phase: RinsePhase::Fill,
                    cycles_left: u32::from(*cycles - cycle - 1),
                });
                if let Err(e) = stepper.move_steps(ctx, Steps(steps.0), Hz(freq)) {
                    last_err = Some(e);
                    break;
                }
                // Empty phase
                stepper.set_direction(Direction::LiqOut);
                motor_state::set_burette_state_tag(&BuretteState::Rinsing {
                    phase: RinsePhase::Empty,
                    cycles_left: u32::from(*cycles - cycle - 1),
                });
                if let Err(e) = stepper.move_steps(ctx, Steps(-(steps.0.abs())), Hz(freq)) {
                    last_err = Some(e);
                    break;
                }
            }
            // After rinse, set state to Idle
            motor_state::set_burette_state_tag(&BuretteState::Idle);
            let current_pos = stepper.position();
            motor_state::CURRENT_POSITION
                .store(current_pos.0, std::sync::atomic::Ordering::Release);
            motor_state::set_current_volume_ml(0.0);

            // Send final status update
            let status = StatusUpdate {
                state: BuretteState::Idle,
                volume_ml: Ml(0.0),
                operation: BuretteOperation::Rinse,
                details: {
                    let mut s: heapless::String<64> = heapless::String::new();
                    if let Some(ref e) = last_err {
                        let _ =
                            core::fmt::Write::write_fmt(&mut s, format_args!("rinse error: {e:?}"));
                    } else {
                        let _ = s.push_str("rinse complete");
                    }
                    s
                },
                cmd_id,
            };
            let _ = status_tx.send(status);

            // Send response
            let response = if last_err.is_some() {
                CommandResponse::Error {
                    id: cmd_id,
                    message: "rinse_error",
                }
            } else {
                let mut data: crate::application::command::CompactJson =
                    crate::application::command::CompactJson::new();
                let _ = core::fmt::Write::write_fmt(
                    &mut data,
                    format_args!(r#"{{"action":"rinse_complete","cycles":{cycles}}}"#),
                );
                CommandResponse::Single {
                    id: cmd_id,
                    status: "ok",
                    data,
                }
            };
            let _ = response_tx.send((cmd_id, response));
        }
        BuretteCommand::Stop => {
            log::info!("Motor: stop requested");
            if let Err(e) = stepper.stop() {
                log::error!("Motor: stop failed: {e:?}");
            }
            motor_state::set_burette_state_tag(&BuretteState::Idle);
            let status = StatusUpdate {
                state: BuretteState::Idle,
                volume_ml: Ml(0.0),
                operation: BuretteOperation::None,
                details: {
                    let mut s: heapless::String<64> = heapless::String::new();
                    let _ = s.push_str("stopped");
                    s
                },
                cmd_id,
            };
            let _ = status_tx.send(status);
            let mut data: crate::application::command::CompactJson =
                crate::application::command::CompactJson::new();
            let _ =
                core::fmt::Write::write_fmt(&mut data, format_args!(r#"{{"action":"stopped"}}"#));
            let response = CommandResponse::Single {
                id: cmd_id,
                status: "ok",
                data,
            };
            let _ = response_tx.send((cmd_id, response));
        }
        BuretteCommand::EmergencyStop => {
            log::info!("Motor: emergency stop");
            if let Err(e) = stepper.emergency_stop() {
                log::error!("Motor: emergency_stop failed: {e:?}");
            }
            motor_state::set_burette_state_tag(&BuretteState::Error);
            let status = StatusUpdate {
                state: BuretteState::Error,
                volume_ml: Ml(0.0),
                operation: BuretteOperation::None,
                details: {
                    let mut s: heapless::String<64> = heapless::String::new();
                    let _ = s.push_str("emergency_stopped");
                    s
                },
                cmd_id,
            };
            let _ = status_tx.send(status);
            let mut data: crate::application::command::CompactJson =
                crate::application::command::CompactJson::new();
            let _ = core::fmt::Write::write_fmt(
                &mut data,
                format_args!(r#"{{"action":"emergency_stopped"}}"#),
            );
            let response = CommandResponse::Single {
                id: cmd_id,
                status: "ok",
                data,
            };
            let _ = response_tx.send((cmd_id, response));
        }
        BuretteCommand::Reset => {
            log::info!("Motor: reset");
            if let Err(e) = stepper.enable() {
                log::error!("Motor: enable after reset failed: {e:?}");
            }
            motor_state::CURRENT_POSITION.store(0, std::sync::atomic::Ordering::Release);
            motor_state::set_current_volume_ml(0.0);
            motor_state::set_burette_state_tag(&BuretteState::Idle);
            let status = StatusUpdate {
                state: BuretteState::Idle,
                volume_ml: Ml(0.0),
                operation: BuretteOperation::None,
                details: {
                    let mut s: heapless::String<64> = heapless::String::new();
                    let _ = s.push_str("reset");
                    s
                },
                cmd_id,
            };
            let _ = status_tx.send(status);
            let mut data: crate::application::command::CompactJson =
                crate::application::command::CompactJson::new();
            let _ = core::fmt::Write::write_fmt(&mut data, format_args!(r#"{{"action":"reset"}}"#));
            let response = CommandResponse::Single {
                id: cmd_id,
                status: "ok",
                data,
            };
            let _ = response_tx.send((cmd_id, response));
        }
    }
}

/// Execute a single stepper motion, update atomics, and send responses.
///
/// # Lint justification
///
/// `too_many_arguments`: Requires stepper ref, motion params (steps, speed),
/// MotorContext marker, calibration config, status channel, command ID,
/// response channel, working state tag, and operation type. Each parameter
/// is orthogonal and necessary; grouping into a struct would add indirection
/// without simplifying actual data flow.
#[allow(clippy::too_many_arguments)]
fn execute_motion(
    stepper: &mut RmtStepper<'static>,
    steps: Steps,
    speed: Hz,
    ctx: &MotorContext,
    cal_config: &CalibrationConfig,
    status_tx: &mpsc::Sender<StatusUpdate>,
    cmd_id: u64,
    response_tx: &mpsc::SyncSender<(u64, CommandResponse)>,
    working_state: &BuretteState,
    operation: BuretteOperation,
) {
    // Update state to working
    motor_state::set_burette_state_tag(working_state);

    // Execute motion
    let result = stepper.move_steps(ctx, steps, speed);

    // Read back position after motion
    let current_pos = stepper.position();
    motor_state::CURRENT_POSITION.store(current_pos.0, std::sync::atomic::Ordering::Release);

    // Calculate volume from position
    let vol = calibration::steps_to_volume(
        current_pos,
        Steps(0),
        cal_config.steps_per_ml,
        Ml(cal_config.nominal_vol),
    );
    motor_state::set_current_volume_ml(vol.0);

    // Determine final state
    let final_state = match &result {
        Ok(()) => BuretteState::Idle,
        Err(StepperError::LimitSwitchReached) => {
            log::info!("Motor: limit switch reached during motion");
            BuretteState::Idle
        }
        Err(e) => {
            log::error!("Motor: motion error: {e:?}");
            BuretteState::Error
        }
    };
    motor_state::set_burette_state_tag(&final_state);

    // Build status update details
    let detail_str = match &result {
        Ok(()) => "ok",
        Err(StepperError::LimitSwitchReached) => "limit_switch",
        Err(_) => "error",
    };

    // Send status update
    let status = StatusUpdate {
        state: final_state,
        volume_ml: vol,
        operation,
        details: {
            let mut s: heapless::String<64> = heapless::String::new();
            let _ = s.push_str(detail_str);
            s
        },
        cmd_id,
    };
    let _ = status_tx.send(status);

    // Send completion response
    let response = match &result {
        Ok(()) => {
            let mut data: crate::application::command::CompactJson =
                crate::application::command::CompactJson::new();
            let _ = core::fmt::Write::write_fmt(
                &mut data,
                format_args!(
                    r#"{{"action":"complete","vol_ml":{:.2},"steps":{}}}"#,
                    vol.0, current_pos.0,
                ),
            );
            CommandResponse::Single {
                id: cmd_id,
                status: "ok",
                data,
            }
        }
        Err(e) => CommandResponse::Error {
            id: cmd_id,
            message: match e {
                StepperError::LimitSwitchReached => "limit_switch",
                _ => "motor_error",
            },
        },
    };
    let _ = response_tx.send((cmd_id, response));
}

/// Extract the `BuretteOperation` from a `BuretteCommand`.
const fn cmd_operation(cmd: &BuretteCommand) -> BuretteOperation {
    match cmd {
        BuretteCommand::Fill { .. } => BuretteOperation::Fill,
        BuretteCommand::Empty { .. } => BuretteOperation::Empty,
        BuretteCommand::Dose { .. } => BuretteOperation::Dose,
        BuretteCommand::Rinse { .. } => BuretteOperation::Rinse,
        BuretteCommand::Stop | BuretteCommand::EmergencyStop | BuretteCommand::Reset => {
            BuretteOperation::None
        }
    }
}
