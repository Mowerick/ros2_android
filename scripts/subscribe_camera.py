#!/usr/bin/env python3
"""Subscribe to an Android camera image topic and display the feed with OpenCV.

Usage:
    # Auto-discover available camera topics:
    python3 subscribe_camera.py

    # Specify a topic explicitly:
    python3 subscribe_camera.py --topic camera/front/image_color

    # Set domain ID (must match the Android app):
    ROS_DOMAIN_ID=42 python3 subscribe_camera.py

Dependencies:
    pip install opencv-python numpy
    # Plus a sourced ROS 2 Humble workspace with rclpy and sensor_msgs
"""

import argparse
import sys
import time

import cv2
import numpy as np
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
from sensor_msgs.msg import Image


class CameraSubscriber(Node):
    def __init__(self, topic: str):
        super().__init__("android_camera_viewer")

        # Match the publisher QoS: best-effort, depth 1
        qos = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            history=HistoryPolicy.KEEP_LAST,
            depth=1,
        )

        self.subscription = self.create_subscription(
            Image, topic, self.on_image, qos
        )
        self.get_logger().info(f"Subscribed to '{topic}' (best-effort, depth 1)")
        self.get_logger().info("Press 'q' in the window or Ctrl+C to quit.")

    def on_image(self, msg: Image):
        # Convert sensor_msgs/Image to a numpy array
        if msg.encoding == "rgb8":
            img = np.frombuffer(msg.data, dtype=np.uint8).reshape(
                msg.height, msg.width, 3
            )
            img = cv2.cvtColor(img, cv2.COLOR_RGB2BGR)
        elif msg.encoding in ("bgr8",):
            img = np.frombuffer(msg.data, dtype=np.uint8).reshape(
                msg.height, msg.width, 3
            )
        elif msg.encoding == "mono8":
            img = np.frombuffer(msg.data, dtype=np.uint8).reshape(
                msg.height, msg.width
            )
        else:
            self.get_logger().warn(f"Unsupported encoding: {msg.encoding}")
            return

        cv2.imshow("Android Camera Feed", img)
        if cv2.waitKey(1) & 0xFF == ord("q"):
            self.get_logger().info("Quit requested.")
            raise SystemExit


def discover_camera_topics(node: Node, timeout_sec: float = 5.0) -> list[str]:
    """Wait for topic discovery and return camera image topics."""
    node.get_logger().info(
        f"Discovering camera topics for {timeout_sec}s ..."
    )
    end = time.time() + timeout_sec
    found = set()
    while time.time() < end:
        rclpy.spin_once(node, timeout_sec=0.5)
        for name, types in node.get_topic_names_and_types():
            if "image_color" in name and "sensor_msgs/msg/Image" in types:
                found.add(name)
    return sorted(found)


def main():
    parser = argparse.ArgumentParser(
        description="Display an Android ROS 2 camera feed."
    )
    parser.add_argument(
        "--topic", type=str, default=None,
        help="Image topic to subscribe to (e.g. camera/front/image_color). "
             "If omitted, auto-discovers available topics.",
    )
    args = parser.parse_args()

    rclpy.init()

    if args.topic:
        topic = args.topic
    else:
        tmp_node = rclpy.create_node("_camera_discovery")
        topics = discover_camera_topics(tmp_node)
        tmp_node.destroy_node()

        if not topics:
            print("No camera image topics found. Is the Android app publishing?")
            rclpy.shutdown()
            sys.exit(1)

        if len(topics) == 1:
            topic = topics[0]
            print(f"Found topic: {topic}")
        else:
            print("Available camera topics:")
            for i, t in enumerate(topics):
                print(f"  [{i}] {t}")
            choice = input("Select topic number: ").strip()
            try:
                topic = topics[int(choice)]
            except (ValueError, IndexError):
                print("Invalid selection.")
                rclpy.shutdown()
                sys.exit(1)

    node = CameraSubscriber(topic)
    try:
        rclpy.spin(node)
    except (KeyboardInterrupt, SystemExit):
        pass
    finally:
        cv2.destroyAllWindows()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
