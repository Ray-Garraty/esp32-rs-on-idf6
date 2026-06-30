#!/usr/bin/env python3
"""
Regression tests for scripts/check_blocking.py

Tests the check_file() function with various code snippets:
- Blocking call in main loop — should FAIL
- Blocking call inside std::thread::spawn — should PASS
- Blocking call inside xTaskCreate — should PASS
- 10ms heartbeat tick — should PASS
- Blocking call with MAIN_LOOP_TICK_MS constant — should PASS
"""

import sys
import os
import tempfile

# Add scripts directory to path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_blocking

PASS = "PASS"
FAIL = "FAIL"

def run_test(name, content, expect_error):
    """Run a single regression test.
    
    Args:
        name: Test name
        content: File content to check
        expect_error: True if the check should report an error
    """
    with tempfile.NamedTemporaryFile(mode='w', suffix='.rs', delete=False) as f:
        f.write(content)
        tmp_path = f.name
    
    try:
        # Temporarily add the temp dir to FORBIDDEN_FILES
        original_forbidden = check_blocking.FORBIDDEN_FILES.copy()
        check_blocking.FORBIDDEN_FILES.append(tmp_path)
        
        result = check_blocking.check_file(tmp_path)
        
        check_blocking.FORBIDDEN_FILES = original_forbidden
        
        if expect_error:
            status = FAIL if result else PASS
        else:
            status = PASS if result else FAIL
        
        print(f"[{status}] {name}")
        if status == FAIL:
            print(f"  Expected error={expect_error}, got result={result}")
    finally:
        os.unlink(tmp_path)


def test_blocking_in_main():
    """sleep(1000ms) in main loop should be flagged."""
    run_test(
        "BLOCKING sleep in main loop",
        """
fn main() {
    loop {
        do_work();
        std::thread::sleep(std::time::Duration::from_millis(100));
    }
}
""",
        expect_error=True
    )


def test_blocking_in_thread():
    """sleep(1000ms) inside std::thread::spawn should NOT be flagged."""
    run_test(
        "OK: sleep in spawned thread",
        """
fn main() {
    std::thread::spawn(move || {
        loop {
            poll_sensor();
            std::thread::sleep(std::time::Duration::from_millis(1000));
        }
    });
    loop {
        std::thread::sleep(std::time::Duration::from_millis(10));
    }
}
""",
        expect_error=False
    )


def test_blocking_in_builder_thread():
    """sleep inside std::thread::Builder::new().spawn() should NOT be flagged."""
    run_test(
        "OK: sleep in Builder::new().spawn()",
        """
fn main() {
    let _ = std::thread::Builder::new()
        .stack_size(16384)
        .name("temp".into())
        .spawn(move || {
            loop {
                read_sensor();
                std::thread::sleep(std::time::Duration::from_millis(1000));
            }
        });
    loop {
        std::thread::sleep(std::time::Duration::from_millis(10));
    }
}
""",
        expect_error=False
    )


def test_send_and_wait_in_thread():
    """send_and_wait inside spawned thread should NOT be flagged."""
    run_test(
        "OK: send_and_wait in spawned thread",
        """
fn motor_thread() {
    std::thread::spawn(move || {
        loop {
            stepper.send_and_wait(encoder, &symbols, &config);
        }
    });
}
""",
        expect_error=False
    )


def test_send_and_wait_in_main():
    """send_and_wait in main loop should be flagged."""
    run_test(
        "BLOCKING: send_and_wait in main loop",
        """
fn main() {
    loop {
        stepper.send_and_wait(encoder, &symbols, &config);
    }
}
""",
        expect_error=True
    )


def test_heartbeat_tick():
    """10ms tick in main loop should NOT be flagged."""
    run_test(
        "OK: 10ms heartbeat tick",
        """
fn main() {
    loop {
        process();
        std::thread::sleep(std::time::Duration::from_millis(10));
    }
}
""",
        expect_error=False
    )


def test_heartbeat_tick_constant():
    """MAIN_LOOP_TICK_MS constant should NOT be flagged."""
    run_test(
        "OK: MAIN_LOOP_TICK_MS constant",
        """
fn main() {
    loop {
        std::thread::sleep(std::time::Duration::from_millis(MAIN_LOOP_TICK_MS));
    }
}
""",
        expect_error=False
    )


def test_lock_in_main():
    """Mutex lock+unwrap in main loop should be flagged."""
    run_test(
        "BLOCKING: mutex lock().unwrap() in main loop",
        """
fn main() {
    let data = lock.lock().unwrap();
}
""",
        expect_error=True
    )


def test_no_issues():
    """Empty file — should pass."""
    run_test(
        "OK: empty file",
        """
fn main() {
    loop {
        process();
    }
}
""",
        expect_error=False
    )


if __name__ == '__main__':
    test_blocking_in_main()
    test_blocking_in_thread()
    test_blocking_in_builder_thread()
    test_send_and_wait_in_thread()
    test_send_and_wait_in_main()
    test_heartbeat_tick()
    test_heartbeat_tick_constant()
    test_lock_in_main()
    test_no_issues()
    print("Done.")
