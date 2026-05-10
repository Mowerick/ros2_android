#!/usr/bin/env python3
"""
Mock ESP32 firmware node for testing TargetManagerController without hardware.

Subscribes:  /ESP32_Command  (vermin_collector_ros_msgs/Command)
Publishes:   /ESP32_Feedback (vermin_collector_ros_msgs/Feedback)

Simulates the ESP32 state machine:
  READY -> (on any move command) -> MOVING (for move_duration_s) -> READY

For SETUP commands: echoes frequency_goals/en_motors/resolution back in Feedback
so the Android SetupEchoed() check passes immediately (no movement delay).

Usage:
    source /opt/ros/humble/setup.bash
    python3 mock_esp32_node.py

Then run the Android app and enable the Target Manager. Watch state progression
in logcat: make logcat | grep -iE "INIT|HARDHOME|SETUP_PHASE|CALIBRAT|SOFTHOME|READY|TARGET"
"""

import rclpy
from rclpy.node import Node
from vermin_collector_ros_msgs.msg import Command, Feedback
import threading
import time


# How long each command type keeps firmware in MOVING state (seconds)
MOVE_DURATION = {
    Command.HARD_HOMING: 2.0,
    Command.SOFT_HOMING: 1.5,
    Command.HOMING: 2.0,
    Command.TARGET: 1.0,
    Command.SETUP: 0.0,  # no movement; echo back config immediately
}

CMD_NAMES = {
    Command.SETUP: 'SETUP',
    Command.TARGET: 'TARGET',
    Command.HOMING: 'HOMING',
    Command.SOFT_HOMING: 'SOFT_HOMING',
    Command.HARD_HOMING: 'HARD_HOMING',
}


class MockEsp32(Node):
    def __init__(self):
        super().__init__('mock_esp32')
        self._pub = self.create_publisher(Feedback, '/ESP32_Feedback', 10)
        self._sub = self.create_subscription(
            Command, '/ESP32_Command', self._on_command, 10)
        # 10 Hz feedback publisher
        self._timer = self.create_timer(0.1, self._publish_feedback)

        self._lock = threading.Lock()
        # Firmware state
        self._moving = False
        self._current_steps = [0, 0, 0]
        self._frequencies = [1000, 1000, 1000]
        self._en_motors = [0, 0, 0]
        self._resolution = 8

        self.get_logger().info(
            'Mock ESP32 ready. Publishing /ESP32_Feedback at 10 Hz, '
            'listening on /ESP32_Command.'
        )

    def _on_command(self, msg: Command):
        name = CMD_NAMES.get(msg.command_type, f'type={msg.command_type}')
        self.get_logger().info(
            f'Received {name}: steps={list(msg.step_goals)} '
            f'freq={list(msg.frequency_goals)} '
            f'en={list(msg.en_motors)} res={msg.resolution}'
        )

        if msg.command_type == Command.SETUP:
            # Echo back config immediately - no movement
            # Cast to plain int: ROS message fields return numpy integers which
            # are rejected by the Feedback message setter validation.
            with self._lock:
                self._frequencies = [int(v) for v in msg.frequency_goals]
                self._en_motors = [int(v) for v in msg.en_motors]
                self._resolution = int(msg.resolution)
            self.get_logger().info(
                f'  -> SETUP echoed: freq={self._frequencies} '
                f'en={self._en_motors} res={self._resolution}'
            )
            return

        duration = MOVE_DURATION.get(msg.command_type, 1.0)
        target_steps = [int(v) for v in msg.step_goals] if msg.command_type == Command.TARGET else [0, 0, 0]
        threading.Thread(
            target=self._do_move,
            args=(duration, target_steps, name),
            daemon=True,
        ).start()

    def _do_move(self, duration: float, target_steps: list, cmd_name: str):
        with self._lock:
            self._moving = True
        self.get_logger().info(f'  -> MOVING for {duration}s ({cmd_name})')
        time.sleep(duration)
        with self._lock:
            self._current_steps = target_steps
            self._moving = False
        self.get_logger().info(f'  -> READY (steps={target_steps})')

    def _publish_feedback(self):
        fb = Feedback()
        with self._lock:
            fb.state = 2 if self._moving else 0  # MOVING=2, READY=0
            fb.current_steps = self._current_steps
            fb.frequencies = self._frequencies
            fb.en_motors = self._en_motors
            fb.resolution = self._resolution
        self._pub.publish(fb)


def main():
    rclpy.init()
    node = MockEsp32()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
