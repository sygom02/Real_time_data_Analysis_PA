from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def _bool_param(name):
    return ParameterValue(LaunchConfiguration(name), value_type=bool)


def _float_param(name):
    return ParameterValue(LaunchConfiguration(name), value_type=float)


def _int_param(name):
    return ParameterValue(LaunchConfiguration(name), value_type=int)


def generate_launch_description():
    map_yaml_file = LaunchConfiguration("map_yaml_file")
    scan_topic = LaunchConfiguration("scan_topic")

    args = [
        DeclareLaunchArgument(
            "map_yaml_file",
            default_value=PathJoinSubstitution(
                [FindPackageShare("cartographer_parallel"), "maps", "0501.yaml"]
            ),
        ),
        DeclareLaunchArgument("scan_topic", default_value="scan"),
        DeclareLaunchArgument("initial_x", default_value="-2.0"),
        DeclareLaunchArgument("initial_y", default_value="6.82"),
        DeclareLaunchArgument("initial_yaw", default_value="-3.0255282583321743"),
        DeclareLaunchArgument("full_map_search", default_value="false"),
        DeclareLaunchArgument("linear_search_window", default_value="3.0"),
        DeclareLaunchArgument("global_search_window", default_value="20.0"),
        DeclareLaunchArgument("angular_search_window", default_value="0.35"),
        DeclareLaunchArgument("angular_step", default_value="0.05"),
        DeclareLaunchArgument("branch_and_bound_depth", default_value="4"),
        DeclareLaunchArgument("min_score", default_value="0.05"),
        DeclareLaunchArgument("publish_top_candidates", default_value="150"),
        DeclareLaunchArgument("global_first_match", default_value="false"),
        DeclareLaunchArgument("global_every_n", default_value="0"),
        DeclareLaunchArgument("perf_summary_idle_seconds", default_value="2.0"),
        DeclareLaunchArgument("perf_marker_period_seconds", default_value="0.5"),
        DeclareLaunchArgument("perf_summary_live_period_seconds", default_value="1.0"),
        DeclareLaunchArgument("perf_csv_file", default_value=""),
        DeclareLaunchArgument("perf_summary_log_file", default_value=""),
        DeclareLaunchArgument("score_all_backend", default_value="cpu_baseline"),
    ]

    node = Node(
        package="cartographer_parallel",
        executable="fast_correlative_node",
        name="fast_correlative_node",
        output="screen",
        parameters=[
            {
                "map_yaml_file": map_yaml_file,
                "scan_topic": scan_topic,
                "map_frame": "map",
                "base_frame": "base_link",
                "initial_x": _float_param("initial_x"),
                "initial_y": _float_param("initial_y"),
                "initial_yaw": _float_param("initial_yaw"),
                "global_first_match": _bool_param("global_first_match"),
                "global_every_n": _int_param("global_every_n"),
                "linear_search_window": _float_param("linear_search_window"),
                "global_search_window": _float_param("global_search_window"),
                "full_map_search": _bool_param("full_map_search"),
                "angular_search_window": _float_param("angular_search_window"),
                "angular_step": _float_param("angular_step"),
                "branch_and_bound_depth": _int_param("branch_and_bound_depth"),
                "min_score": _float_param("min_score"),
                "publish_top_candidates": _int_param("publish_top_candidates"),
                "max_candidates": _int_param("publish_top_candidates"),
                "perf_summary_idle_seconds": _float_param(
                    "perf_summary_idle_seconds"
                ),
                "perf_marker_period_seconds": _float_param(
                    "perf_marker_period_seconds"
                ),
                "perf_summary_live_period_seconds": _float_param(
                    "perf_summary_live_period_seconds"
                ),
                "perf_csv_file": LaunchConfiguration("perf_csv_file"),
                "perf_summary_log_file": LaunchConfiguration(
                    "perf_summary_log_file"
                ),
                "score_all_backend": LaunchConfiguration("score_all_backend"),
            }
        ],
    )

    return LaunchDescription(args + [node])
