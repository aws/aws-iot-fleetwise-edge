# rosigen (ROS2 Interactive Generator)

- Generates ROS2 messages according to ROS2 interface descriptions
- Let's you interactively set signal values
- Has autocompletion

## Prerequisites

In addition to the ROS2 `rclpy` which is imported from the ROS2 environment, the pip package
`prompt_toolkit` is required.

## Quick Start

Starting in your ROS2 workspace directory and having already built the interface description
packages using `colcon`, source the ROS2 and local environments:

```bash
source /opt/ros/humble/setup.bash
source install/local_setup.bash
```

The second line is only needed if you use custom messages not part of pre-installed message like
`sensor_msgs.msg` installed by package `ros-humble-sensor-msgs`.

Change to this folder, then run:

```
python3 rosigen.py --config config.json --values vals_default.json
```

- Set a signal value (note that space is used as the message delimiter):

  ```
  set ImageTopic header stamp sec 123
  ```

- Get a signal value (note that space is used as the message delimiter):

  ```
  get ImageTopic header stamp sec
  ```

- Save values to a JSON file:

  ```
  save vals.json
  ```

- Load values from a JSON file:

  ```
  load vals.json
  ```

## Configuration

The configuration file is in the following format, where:

- `<TOPIC_NAME>` is the topic name **without** the leading `/`, e.g. `ImageTopic`
- `<MESSAGE_TYPE_MODULE>` is the name of the Python module containing the message type, e.g.
  `sensor_msgs.msg`
- `<MESSAGE_TYPE>` is the ROS2 interface description type, e.g. `Image`
- `<PERIOD>` is the period to send the message in seconds, e.g. `0.5`

```json
{
    "topics": [
        {"name": "<TOPIC_NAME>", "module": "<MESSAGE_TYPE_MODULE>", "type": "<MESSAGE_TYPE>", "period_sec": <PERIOD>},
        ...
    ]
}
```
