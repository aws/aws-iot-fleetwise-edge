# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import rclpy
from rclpy.node import Node
from ros2_custom_messages.msg import (
    PumpUpMessageMain,
    PumpUpMessageNested1,
    PumpUpMessageNested2,
    PumpUpMessageNestedEnd,
)


class MinimalPublisher(Node):
    def __init__(self):
        self.factor = 100
        super().__init__("minimal_publisher")
        self.publisher_ = self.create_publisher(PumpUpMessageMain, "testtopic", 10)
        timer_period = 0.5  # seconds
        self.timer = self.create_timer(timer_period, self.timer_callback)
        self.i = 0
        nested_end = PumpUpMessageNestedEnd()
        nested_end.very_long_name_to_get_the_very_verbose_json_pumped_up_to_more_than_factor_hundred_of_original_cdr_end = (  # noqa: E501
            True
        )
        nested_2 = PumpUpMessageNested2()
        nested_2.very_long_name_to_get_the_very_verbose_json_pumped_up_to_more_than_factor_hundred_of_original_cdr_nested2 = (  # noqa: E501
            nested_end
        )
        nested_1 = PumpUpMessageNested1()
        nested_1.very_long_name_to_get_the_very_verbose_json_pumped_up_to_more_than_factor_hundred_of_original_cdr_nested1 = (  # noqa: E501
            nested_2
        )
        self.message_to_send = PumpUpMessageMain()
        self.message_to_send.very_long_name_to_get_the_very_verbose_json_pumped_up_to_more_than_factor_hundred_of_original_cdr = [  # noqa: E501
            nested_1
        ] * self.factor

    def timer_callback(self):
        self.publisher_.publish(self.message_to_send)
        self.get_logger().info(
            "Publishing data with factor: "
            + str(self.factor)
            + " which should result in CDR ~ "
            + str(self.factor)
        )


def main(args=None):
    rclpy.init(args=args)

    minimal_publisher = MinimalPublisher()

    rclpy.spin(minimal_publisher)

    # Destroy the node explicitly
    # (optional - otherwise it will be done automatically
    # when the garbage collector destroys the node object)
    minimal_publisher.destroy_node()
    rclpy.shutdown()


if __name__ == "__main__":
    main()
