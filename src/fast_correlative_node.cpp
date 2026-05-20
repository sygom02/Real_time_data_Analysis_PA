#include "cartographer_parallel/fast_matcher.h"

#include "cartographer_parallel/assignment.h"

#include <geometry_msgs/Pose.h>
#include <geometry_msgs/PoseArray.h>
#include <geometry_msgs/Quaternion.h>
#include <nav_msgs/OccupancyGrid.h>
#include <nav_msgs/Odometry.h>
#include <ros/ros.h>
#include <sensor_msgs/LaserScan.h>
#include <std_msgs/Float32.h>
#include <std_msgs/String.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cartographer_parallel_ros {
namespace {

geometry_msgs::Quaternion YawToQuat(const double yaw) {
  geometry_msgs::Quaternion q;
  q.x = 0.0;
  q.y = 0.0;
  q.z = std::sin(0.5 * yaw);
  q.w = std::cos(0.5 * yaw);
  return q;
}

geometry_msgs::Pose ToPose(const cartographer_parallel::CandOut& cand) {
  geometry_msgs::Pose pose;
  pose.position.x = cand.x;
  pose.position.y = cand.y;
  pose.position.z = 0.0;
  pose.orientation = YawToQuat(cand.yaw);
  return pose;
}

std::string StampToString(const ros::Time& stamp) {
  std::ostringstream out;
  out << stamp.sec << "." << std::setw(9) << std::setfill('0') << stamp.nsec;
  return out.str();
}

std::string SanitizeFileToken(std::string token) {
  if (token.empty() || token == "/") return "root";
  for (char& c : token) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
          c == '-')) {
      c = '_';
    }
  }
  while (!token.empty() && token.front() == '_') token.erase(token.begin());
  return token.empty() ? "root" : token;
}

}  // namespace

class FastCorrelativeNode {
 public:
  FastCorrelativeNode() : nh_(), private_nh_("~") {
    std::string map_yaml;
    private_nh_.param<std::string>("map_yaml_file", map_yaml, "");
    std::string scan_topic;
    private_nh_.param<std::string>("scan_topic", scan_topic, "scan");
    private_nh_.param<std::string>("map_frame", map_frame_, "map");
    private_nh_.param<std::string>("base_frame", base_frame_, "base_link");
    private_nh_.param<double>("initial_x", pose_.x, 0.0);
    private_nh_.param<double>("initial_y", pose_.y, 0.0);
    private_nh_.param<double>("initial_yaw", pose_.yaw, 0.0);
    initial_pose_ = pose_;
    private_nh_.param<bool>("global_first_match", global_first_match_, true);
    private_nh_.param<int>("global_every_n", global_every_n_, 0);
    private_nh_.param<int>("publish_top_candidates", publish_top_candidates_,
                           100);
    private_nh_.param<int>("initial_publish_top_candidates",
                           initial_publish_top_candidates_, 500);

    double initial_min_score = 0.90;
    private_nh_.param<double>("initial_min_score", initial_min_score, 0.90);
    initial_min_score_ = static_cast<float>(initial_min_score);
    private_nh_.param<double>("perf_summary_idle_seconds",
                           perf_summary_idle_seconds_, 2.0);
    private_nh_.param<double>("perf_marker_period_seconds",
                           perf_marker_period_seconds_, 0.5);
    private_nh_.param<double>("perf_summary_live_period_seconds",
                           perf_summary_live_period_seconds_, 1.0);
    private_nh_.param<std::string>("perf_csv_file", perf_csv_file_, "");
    if (perf_csv_file_.empty()) {
      perf_csv_file_ = "/tmp/fast_correlative_perf_" +
                       SanitizeFileToken(ros::this_node::getNamespace()) +
                       ".csv";
    }
    private_nh_.param<std::string>("perf_summary_log_file",
                                   perf_summary_log_file_, "");
    if (perf_summary_log_file_.empty()) {
      perf_summary_log_file_ = "/tmp/fast_correlative_summary_" +
                               SanitizeFileToken(
                                   ros::this_node::getNamespace()) +
                               ".log";
    }

    std::string score_all_backend;
    private_nh_.param<std::string>("score_all_backend", score_all_backend,
                                   "cpu_baseline");
    if (!cartographer_parallel::set_score_all_backend(score_all_backend)) {
      ROS_WARN(
          "Unknown score_all_backend='%s'. Choices: %s. Falling back to "
          "cpu_baseline.",
          score_all_backend.c_str(),
          cartographer_parallel::score_all_backend_choices().c_str());
      cartographer_parallel::set_score_all_backend("cpu_baseline");
    }
    int cuda_block_threads = 256;
    private_nh_.param<int>("score_all_cuda_block_threads", cuda_block_threads,
                           256);
    cartographer_parallel::set_score_all_cuda_block_threads(
        cuda_block_threads);
    ROS_INFO("score_all_backend requested=%s cuda_build=%s cuda_block_threads=%d",
             cartographer_parallel::score_all_backend_name(),
             cartographer_parallel::score_all_cuda_available() ? "available"
                                                               : "unavailable",
             cartographer_parallel::score_all_cuda_block_threads());

    cartographer_parallel::MatchOpt opt;
    private_nh_.param<double>("linear_search_window", opt.linear_window, 3.0);
    private_nh_.param<double>("global_search_window", opt.global_window, 20.0);
    private_nh_.param<bool>("full_map_search", opt.full_map_search, false);
    private_nh_.param<double>("angular_search_window", opt.angular_window,
                           0.35);
    private_nh_.param<double>("angular_step", opt.angular_step, 0.05);
    private_nh_.param<int>("branch_and_bound_depth", opt.branch_depth, 4);
    double min_score = 0.05;
    private_nh_.param<double>("min_score", min_score, 0.05);
    opt.min_score = static_cast<float>(min_score);
    private_nh_.param<int>("max_candidates", opt.max_cand,
                           publish_top_candidates_);
    matcher_.SetOptions(opt);

    cartographer_parallel::MatchOpt initial_opt = opt;
    private_nh_.param<double>("initial_global_search_window",
                              initial_opt.global_window, 30.0);
    private_nh_.param<bool>("initial_full_map_search",
                            initial_opt.full_map_search, true);
    private_nh_.param<double>("initial_angular_search_window",
                              initial_opt.angular_window, 3.15);
    private_nh_.param<double>("initial_angular_step", initial_opt.angular_step,
                           0.035);
    private_nh_.param<int>("initial_branch_and_bound_depth",
                           initial_opt.branch_depth, 3);
    initial_opt.max_cand = initial_publish_top_candidates_;
    initial_matcher_.SetOptions(initial_opt);

    cartographer_parallel::MatchOpt initial_refine_opt = opt;
    private_nh_.param<double>("initial_refine_linear_window",
                              initial_refine_opt.linear_window, 1.0);
    private_nh_.param<double>("initial_refine_angular_search_window",
                              initial_refine_opt.angular_window, 0.08);
    private_nh_.param<double>("initial_refine_angular_step",
                              initial_refine_opt.angular_step, 0.005);
    private_nh_.param<int>("initial_refine_branch_and_bound_depth",
                           initial_refine_opt.branch_depth, 2);
    initial_refine_opt.full_map_search = false;
    initial_refine_opt.max_cand = initial_publish_top_candidates_;
    initial_refine_matcher_.SetOptions(initial_refine_opt);

    if (map_yaml.empty() || !matcher_.LoadMap(map_yaml) ||
        !initial_matcher_.LoadMap(map_yaml) ||
        !initial_refine_matcher_.LoadMap(map_yaml)) {
      ROS_FATAL("Failed to load map yaml: %s", map_yaml.c_str());
      throw std::runtime_error("failed to load map");
    }

    ROS_INFO("Loaded map %s (%dx%d, resolution %.3f)", map_yaml.c_str(),
             matcher_.width(), matcher_.height(), matcher_.resolution());
    ROS_INFO(
        "Initial search: full_map=%s, global_window=%.3f, angular_window=%.3f, "
        "angular_step=%.3f, branch_depth=%d, accept_score=%.3f",
        initial_opt.full_map_search ? "true" : "false",
        initial_opt.global_window, initial_opt.angular_window,
        initial_opt.angular_step, initial_opt.branch_depth, initial_min_score_);
    ROS_INFO("Initial refine: linear_window=%.3f, angular_window=%.3f, "
             "angular_step=%.3f, branch_depth=%d",
             initial_refine_opt.linear_window,
             initial_refine_opt.angular_window,
             initial_refine_opt.angular_step,
             initial_refine_opt.branch_depth);
    openPerfCsv();
    openPerfSummaryLog();

    map_pub_ = nh_.advertise<nav_msgs::OccupancyGrid>("map", 1, true);
    odom_pub_ = nh_.advertise<nav_msgs::Odometry>("fast_correlative_odom", 10);
    candidate_pub_ =
        nh_.advertise<geometry_msgs::PoseArray>("fast_correlative_candidates", 10);
    marker_pub_ =
        nh_.advertise<visualization_msgs::MarkerArray>("fast_correlative_markers", 10);
    candidate_text_pub_ =
        nh_.advertise<std_msgs::String>("fast_correlative_candidate_text", 10);
    match_ms_pub_ = nh_.advertise<std_msgs::Float32>("perf/match_ms", 10);
    score_all_ms_pub_ =
        nh_.advertise<std_msgs::Float32>("perf/score_all_ms", 10);
    match_ms_avg_pub_ =
        nh_.advertise<std_msgs::Float32>("perf/match_ms_avg", 10);
    score_all_ms_avg_pub_ =
        nh_.advertise<std_msgs::Float32>("perf/score_all_ms_avg", 10);
    perf_summary_json_pub_ =
        nh_.advertise<std_msgs::String>("perf/summary_json", 1, true);
    perf_marker_pub_ =
        nh_.advertise<visualization_msgs::Marker>("perf/text_marker", 10);

    scan_sub_ = nh_.subscribe(scan_topic, 10, &FastCorrelativeNode::scanCallback,
                              this);
    map_timer_ = nh_.createTimer(ros::Duration(1.0),
                                 &FastCorrelativeNode::publishMapTimer, this);
    perf_summary_timer_ =
        nh_.createWallTimer(ros::WallDuration(0.25),
                            &FastCorrelativeNode::maybePublishFinalSummaryTimer,
                            this);

    publishMap();
  }

 private:
  struct PerfStats {
    double match_ms = 0.0;
    double score_all_ms = 0.0;
    int score_all_calls = 0;
    int score_all_candidates = 0;
    int score_all_evaluated_candidates = 0;
    int score_all_scan_points = 0;
    long long score_all_cell_checks = 0;
    long long score_all_evaluated_cell_checks = 0;
    float score = 0.0f;
  };

  static double RatioPct(const double part, const double total) {
    return total > 0.0 ? 100.0 * part / total : 0.0;
  }

  static double ThroughputMChecksPerSec(const long long cell_checks,
                                        const double elapsed_ms) {
    return elapsed_ms > 0.0
               ? static_cast<double>(cell_checks) / elapsed_ms / 1000.0
               : 0.0;
  }

  static void AddScoreStats(const cartographer_parallel::MatchOut& match,
                            PerfStats* const stats) {
    if (stats == nullptr) return;
    stats->score_all_ms += match.score_all_ms;
    stats->score_all_calls += match.score_all_calls;
    stats->score_all_candidates += match.score_all_candidates;
    stats->score_all_evaluated_candidates +=
        match.score_all_evaluated_candidates;
    stats->score_all_scan_points += match.score_all_scan_points;
    stats->score_all_cell_checks += match.score_all_cell_checks;
    stats->score_all_evaluated_cell_checks +=
        match.score_all_evaluated_cell_checks;
  }

  static PerfStats MakePerfStats(const double match_ms,
                                 const cartographer_parallel::MatchOut& match) {
    PerfStats stats;
    stats.match_ms = match_ms;
    stats.score = match.score;
    AddScoreStats(match, &stats);
    return stats;
  }

  void publishMapTimer(const ros::TimerEvent&) { publishMap(); }

  void publishMap() {
    nav_msgs::OccupancyGrid msg;
    msg.header.stamp = ros::Time::now();
    msg.header.frame_id = map_frame_;
    msg.info.resolution = matcher_.resolution();
    msg.info.width = matcher_.width();
    msg.info.height = matcher_.height();
    msg.info.origin.position.x = matcher_.origin_x();
    msg.info.origin.position.y = matcher_.origin_y();
    msg.info.origin.orientation.w = 1.0;
    msg.data.resize(msg.info.width * msg.info.height);

    const std::vector<unsigned char>& map = matcher_.map();
    const int w = matcher_.width();
    const int h = matcher_.height();
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const int src = y * w + x;
        const int dst = (h - 1 - y) * w + x;
        msg.data[dst] = static_cast<int8_t>(
            std::min(100, static_cast<int>(
                              std::lround(map[src] * 100.0 / 255.0))));
      }
    }
    map_pub_.publish(msg);
  }

  void scanCallback(const sensor_msgs::LaserScanConstPtr& msg) {
    saw_scan_ = true;
    summary_printed_ = false;
    last_scan_wall_time_ = std::chrono::steady_clock::now();

    std::vector<float> xs;
    std::vector<float> ys;
    xs.reserve(msg->ranges.size());
    ys.reserve(msg->ranges.size());
    for (size_t i = 0; i < msg->ranges.size(); ++i) {
      const float r = msg->ranges[i];
      if (!std::isfinite(r) || r <= 0.0f) {
        continue;
      }
      const float a =
          msg->angle_min + static_cast<float>(i) * msg->angle_increment;
      xs.push_back(r * std::cos(a));
      ys.push_back(r * std::sin(a));
    }
    if (xs.empty()) {
      writePerfCsvRow("skip_empty_scan", msg->header.stamp, xs.size(),
                      msg->ranges.size(), false, false, pose_, PerfStats());
      ROS_WARN_STREAM_THROTTLE(
          2.0, "Received scan but no usable ranges. ranges="
                   << msg->ranges.size() << " range_min=" << msg->range_min
                   << " range_max=" << msg->range_max);
      return;
    }

    const auto match_start = std::chrono::steady_clock::now();
    cartographer_parallel::MatchOut out;
    if (!has_pose_ && global_first_match_) {
      PerfStats perf;
      cartographer_parallel::MatchOut broad_out;
      const bool broad_ok =
          initial_matcher_.Match(xs, ys, initial_pose_, true, &broad_out);
      AddScoreStats(broad_out, &perf);
      out = broad_out;
      if (broad_ok) {
        cartographer_parallel::MatchOut refine_out;
        const bool refine_ok = initial_refine_matcher_.Match(
            xs, ys, broad_out.pose, false, &refine_out);
        AddScoreStats(refine_out, &perf);
        if (refine_ok && refine_out.score > out.score) {
          out = refine_out;
        }
      }
      const bool locked = out.ok && out.score >= initial_min_score_;
      ++scan_count_;
      if (locked) {
        pose_ = out.pose;
        has_pose_ = true;
        ROS_INFO("Initial pose locked at score %.3f pose=(%.3f, %.3f, %.3f)",
                 out.score, pose_.x, pose_.y, pose_.yaw);
      } else if (scan_count_ % 10 == 0) {
        ROS_WARN("Waiting for high-confidence initial match. score=%.3f broad=%.3f required=%.3f",
                 out.score, broad_out.score, initial_min_score_);
      }

      perf.match_ms =
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - match_start)
              .count();
      perf.score = out.score;
      ROS_INFO_STREAM_THROTTLE(2.0, PerfLine(perf));
      writePerfCsvRow("initial_match", msg->header.stamp, xs.size(),
                      msg->ranges.size(), out.ok, locked, out.pose, perf);
      publishPerformance(msg->header.stamp, out.pose, perf);
      publishCandidates(msg->header.stamp, out.cand,
                        initial_publish_top_candidates_, perf);
      if (locked) publishOdom(msg->header.stamp, out.pose, out.score);
      return;
    }

    const bool global =
        global_every_n_ > 0 && scan_count_ % global_every_n_ == 0;
    const bool ok = matcher_.Match(xs, ys, pose_, global, &out);
    ++scan_count_;
    if (!ok) {
      if (scan_count_ % 10 == 0) {
        ROS_WARN("Fast correlative match below min_score.");
      }
    } else {
      pose_ = out.pose;
      has_pose_ = true;
    }

    const double match_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - match_start)
            .count();
    const PerfStats perf = MakePerfStats(match_ms, out);
    ROS_INFO_STREAM_THROTTLE(2.0, PerfLine(perf));
    writePerfCsvRow("match", msg->header.stamp, xs.size(), msg->ranges.size(),
                    ok, ok, out.pose, perf);
    publishPerformance(msg->header.stamp, out.pose, perf);
    publishCandidates(msg->header.stamp, out.cand, publish_top_candidates_,
                      perf);
    if (ok) publishOdom(msg->header.stamp, out.pose, out.score);
  }

  void publishOdom(const ros::Time& stamp,
                   const cartographer_parallel::Pose2& pose,
                   const float score) {
    nav_msgs::Odometry odom;
    odom.header.stamp = stamp;
    odom.header.frame_id = map_frame_;
    odom.child_frame_id = base_frame_;
    odom.pose.pose.position.x = pose.x;
    odom.pose.pose.position.y = pose.y;
    odom.pose.pose.orientation = YawToQuat(pose.yaw);
    odom.pose.covariance[0] = std::max(1e-4, 1.0 - score);
    odom.pose.covariance[7] = std::max(1e-4, 1.0 - score);
    odom.pose.covariance[35] = std::max(1e-4, 1.0 - score);
    odom_pub_.publish(odom);
  }

  void publishCandidates(
      const ros::Time& stamp,
      const std::vector<cartographer_parallel::CandOut>& cand,
      const int limit,
      const PerfStats& perf) {
    geometry_msgs::PoseArray poses;
    poses.header.stamp = stamp;
    poses.header.frame_id = map_frame_;
    visualization_msgs::MarkerArray markers;

    const int n = std::min(limit, static_cast<int>(cand.size()));
    poses.poses.reserve(n);
    markers.markers.reserve(n);
    for (int i = 0; i < n; ++i) {
      poses.poses.push_back(ToPose(cand[i]));

      visualization_msgs::Marker marker;
      marker.header = poses.header;
      marker.ns = "fast_correlative_candidates";
      marker.id = i;
      marker.type = visualization_msgs::Marker::SPHERE;
      marker.action = visualization_msgs::Marker::ADD;
      marker.pose = poses.poses.back();
      marker.scale.x = 0.12;
      marker.scale.y = 0.12;
      marker.scale.z = 0.12;
      marker.color.r = 1.0f - cand[i].score;
      marker.color.g = cand[i].score;
      marker.color.b = 0.0f;
      marker.color.a = 0.8f;
      marker.lifetime = ros::Duration(0.5);
      markers.markers.push_back(marker);
    }
    candidate_pub_.publish(poses);
    marker_pub_.publish(markers);
    publishCandidateText(stamp, cand, n, perf);
  }

  void publishCandidateText(
      const ros::Time& stamp,
      const std::vector<cartographer_parallel::CandOut>& cand,
      const int limit,
      const PerfStats& perf) {
    std_msgs::String text;
    std::ostringstream out;
    out << "scan_time=" << StampToString(stamp)
        << " candidates=" << std::min(limit, static_cast<int>(cand.size()))
        << " match_ms=" << std::fixed << std::setprecision(2)
        << perf.match_ms << " score_all_ms=" << perf.score_all_ms
        << " score_all_ratio_pct="
        << RatioPct(perf.score_all_ms, perf.match_ms)
        << " score_all_calls=" << perf.score_all_calls
        << " score_all_candidates=" << perf.score_all_candidates
        << " score_all_evaluated_candidates="
        << perf.score_all_evaluated_candidates
        << " score_all_cell_checks=" << perf.score_all_cell_checks
        << " score_all_evaluated_cell_checks="
        << perf.score_all_evaluated_cell_checks
        << " score_all_throughput_mchecks_s="
        << ThroughputMChecksPerSec(perf.score_all_cell_checks,
                                   perf.score_all_ms)
        << "\n";
    out << "id,x,y,yaw,score\n";
    const int n = std::min(limit, static_cast<int>(cand.size()));
    for (int i = 0; i < n; ++i) {
      out << i << "," << std::fixed << std::setprecision(6) << cand[i].x
          << "," << cand[i].y << "," << cand[i].yaw << ","
          << std::setprecision(4) << cand[i].score << "\n";
    }
    text.data = out.str();
    candidate_text_pub_.publish(text);
  }

  void publishPerformance(const ros::Time& stamp,
                          const cartographer_parallel::Pose2& pose,
                          const PerfStats& perf) {
    (void)stamp;
    (void)pose;
    ++perf_count_;
    match_ms_sum_ += perf.match_ms;
    score_all_ms_sum_ += perf.score_all_ms;
    score_sum_ += perf.score;
    score_all_calls_sum_ += perf.score_all_calls;
    score_all_candidates_sum_ += perf.score_all_candidates;
    score_all_evaluated_candidates_sum_ +=
        perf.score_all_evaluated_candidates;
    score_all_scan_points_sum_ += perf.score_all_scan_points;
    score_all_cell_checks_sum_ += perf.score_all_cell_checks;
    score_all_evaluated_cell_checks_sum_ +=
        perf.score_all_evaluated_cell_checks;
    last_perf_ = perf;
    publishLiveSummary();
    const double match_ms_avg = match_ms_sum_ / static_cast<double>(perf_count_);
    const double score_all_ms_avg =
        score_all_ms_sum_ / static_cast<double>(perf_count_);
    const double throughput =
        ThroughputMChecksPerSec(perf.score_all_cell_checks,
                                perf.score_all_ms);
    const double throughput_avg =
        ThroughputMChecksPerSec(score_all_cell_checks_sum_,
                                score_all_ms_sum_);

    std_msgs::Float32 match_msg;
    match_msg.data = static_cast<float>(perf.match_ms);
    match_ms_pub_.publish(match_msg);

    std_msgs::Float32 score_msg;
    score_msg.data = static_cast<float>(perf.score_all_ms);
    score_all_ms_pub_.publish(score_msg);

    std_msgs::Float32 match_avg_msg;
    match_avg_msg.data = static_cast<float>(match_ms_avg);
    match_ms_avg_pub_.publish(match_avg_msg);

    std_msgs::Float32 score_avg_msg;
    score_avg_msg.data = static_cast<float>(score_all_ms_avg);
    score_all_ms_avg_pub_.publish(score_avg_msg);

    const auto marker_now = std::chrono::steady_clock::now();
    if (has_perf_marker_wall_time_) {
      const auto elapsed = std::chrono::duration<double>(
          marker_now - last_perf_marker_wall_time_);
      if (elapsed.count() < perf_marker_period_seconds_) {
        return;
      }
    }
    has_perf_marker_wall_time_ = true;
    last_perf_marker_wall_time_ = marker_now;

    visualization_msgs::Marker marker;
    marker.header.stamp = ros::Time::now();
    marker.header.frame_id = map_frame_;
    marker.ns = "fast_correlative_perf";
    marker.id = 0;
    marker.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = matcher_.origin_x() + 6.5;
    marker.pose.position.y = matcher_.origin_y() + 1.2;
    marker.pose.position.z = 0.5;
    marker.pose.orientation.w = 1.0;
    marker.scale.z = 0.25;
    marker.color.r = 0.0f;
    marker.color.g = 0.0f;
    marker.color.b = 0.0f;
    marker.color.a = 1.0f;
    marker.lifetime = ros::Duration(3.0);

    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << "match " << perf.match_ms << " avg " << match_ms_avg << " ms\n"
        << "score_all " << perf.score_all_ms << " avg "
        << score_all_ms_avg << "ms\n"
        << "ratio " << RatioPct(perf.score_all_ms, perf.match_ms) << "%\n"
        << "thr " << throughput << "M/s avg " << throughput_avg
        << "M/s\n"
        << "score " << std::setprecision(3) << perf.score
        << " samples " << perf_count_ << "\n"
        << "calls " << perf.score_all_calls
        << " cand " << perf.score_all_candidates
        << " eval " << perf.score_all_evaluated_candidates;
    marker.text = out.str();
    perf_marker_pub_.publish(marker);
  }

  void openPerfCsv() {
    if (perf_csv_file_.empty()) return;
    perf_csv_.open(perf_csv_file_.c_str(), std::ios::out | std::ios::trunc);
    if (!perf_csv_) {
      ROS_WARN("Failed to open perf_csv_file: %s", perf_csv_file_.c_str());
      return;
    }
    perf_csv_
        << "event,scan_count,stamp,wall_time,usable_ranges,total_ranges,"
        << "ok,locked,pose_x,pose_y,pose_yaw,score,match_ms,score_all_ms,"
        << "score_all_ratio_pct,score_all_calls,score_all_candidates,"
        << "score_all_evaluated_candidates,score_all_scan_points,"
        << "score_all_cell_checks,score_all_evaluated_cell_checks,"
        << "score_all_throughput_mchecks_s,backend_active,"
        << "cuda_block_threads\n";
    perf_csv_.flush();
    ROS_INFO("Writing performance CSV to %s", perf_csv_file_.c_str());
  }

  void writePerfCsvRow(const std::string& event, const ros::Time& stamp,
                       const size_t usable_ranges, const size_t total_ranges,
                       const bool ok, const bool locked,
                       const cartographer_parallel::Pose2& pose,
                       const PerfStats& perf) {
    if (!perf_csv_) return;
    perf_csv_ << event << "," << scan_count_ << "," << StampToString(stamp)
              << "," << StampToString(ros::Time::now()) << ","
              << usable_ranges << "," << total_ranges << "," << (ok ? 1 : 0)
              << "," << (locked ? 1 : 0) << "," << std::fixed
              << std::setprecision(6) << pose.x << "," << pose.y << ","
              << pose.yaw << "," << std::setprecision(4) << perf.score << ","
              << std::setprecision(3) << perf.match_ms << ","
              << perf.score_all_ms << ","
              << RatioPct(perf.score_all_ms, perf.match_ms) << ","
              << perf.score_all_calls << "," << perf.score_all_candidates
              << "," << perf.score_all_evaluated_candidates << ","
              << perf.score_all_scan_points << ","
              << perf.score_all_cell_checks << ","
              << perf.score_all_evaluated_cell_checks << ","
              << ThroughputMChecksPerSec(perf.score_all_cell_checks,
                                         perf.score_all_ms)
              << ","
              << cartographer_parallel::score_all_active_backend_name()
              << "," << cartographer_parallel::score_all_cuda_block_threads()
              << "\n";
    perf_csv_.flush();
  }

  void openPerfSummaryLog() {
    if (perf_summary_log_file_.empty()) return;
    std::ofstream summary_log(perf_summary_log_file_.c_str(),
                              std::ios::out | std::ios::trunc);
    if (!summary_log) {
      ROS_WARN("Failed to open perf_summary_log_file: %s",
               perf_summary_log_file_.c_str());
      return;
    }
    ROS_INFO("Writing performance summary log to %s",
             perf_summary_log_file_.c_str());
  }

  void writePerfSummaryLog(const std::string& json) {
    if (perf_summary_log_file_.empty()) return;
    std::ofstream summary_log(perf_summary_log_file_.c_str(),
                              std::ios::out | std::ios::trunc);
    if (!summary_log) {
      ROS_WARN_THROTTLE(5.0, "Failed to write perf_summary_log_file: %s",
                        perf_summary_log_file_.c_str());
      return;
    }
    summary_log << "perf_summary_json=" << json << "\n";
    summary_log.flush();
  }

  std::string makePerfSummaryJson() const {
    const double match_ms_avg =
        perf_count_ > 0 ? match_ms_sum_ / static_cast<double>(perf_count_) : 0.0;
    const double score_all_ms_avg =
        perf_count_ > 0 ? score_all_ms_sum_ / static_cast<double>(perf_count_)
                        : 0.0;
    const double score_avg =
        perf_count_ > 0 ? score_sum_ / static_cast<double>(perf_count_) : 0.0;
    const double score_all_ratio_pct =
        RatioPct(score_all_ms_sum_, match_ms_sum_);
    const double throughput_avg =
        ThroughputMChecksPerSec(score_all_cell_checks_sum_,
                                score_all_ms_sum_);
    const double last_ratio_pct =
        RatioPct(last_perf_.score_all_ms, last_perf_.match_ms);
    const double last_throughput =
        ThroughputMChecksPerSec(last_perf_.score_all_cell_checks,
                                last_perf_.score_all_ms);

    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "{";
    out << "\"type\":\"score_all_perf_summary\",";
    out << "\"score_all_backend_requested\":\""
        << cartographer_parallel::score_all_backend_name() << "\",";
    out << "\"score_all_backend_active\":\""
        << cartographer_parallel::score_all_active_backend_name() << "\",";
    out << "\"score_all_cuda_build\":"
        << (cartographer_parallel::score_all_cuda_available() ? "true"
                                                              : "false")
        << ",";
    out << "\"score_all_cuda_block_threads\":"
        << cartographer_parallel::score_all_cuda_block_threads() << ",";
    out << "\"samples\":" << perf_count_ << ",";
    out << "\"match_ms_avg\":" << match_ms_avg << ",";
    out << "\"score_all_ms_avg\":" << score_all_ms_avg << ",";
    out << "\"score_all_ratio_pct\":" << score_all_ratio_pct << ",";
    out << "\"score_all_throughput_mchecks_s_avg\":" << throughput_avg << ",";
    out << "\"score_avg\":" << score_avg << ",";
    out << "\"match_ms_sum\":" << match_ms_sum_ << ",";
    out << "\"score_all_ms_sum\":" << score_all_ms_sum_ << ",";
    out << "\"score_all_calls_total\":" << score_all_calls_sum_ << ",";
    out << "\"score_all_candidates_total\":"
        << score_all_candidates_sum_ << ",";
    out << "\"score_all_evaluated_candidates_total\":"
        << score_all_evaluated_candidates_sum_ << ",";
    out << "\"score_all_scan_points_total\":"
        << score_all_scan_points_sum_ << ",";
    out << "\"score_all_cell_checks_total\":"
        << score_all_cell_checks_sum_ << ",";
    out << "\"score_all_evaluated_cell_checks_total\":"
        << score_all_evaluated_cell_checks_sum_ << ",";
    out << "\"last\":{";
    out << "\"match_ms\":" << last_perf_.match_ms << ",";
    out << "\"score_all_ms\":" << last_perf_.score_all_ms << ",";
    out << "\"score_all_ratio_pct\":" << last_ratio_pct << ",";
    out << "\"score_all_throughput_mchecks_s\":" << last_throughput << ",";
    out << "\"score\":" << last_perf_.score << ",";
    out << "\"score_all_calls\":" << last_perf_.score_all_calls << ",";
    out << "\"score_all_candidates\":"
        << last_perf_.score_all_candidates << ",";
    out << "\"score_all_evaluated_candidates\":"
        << last_perf_.score_all_evaluated_candidates << ",";
    out << "\"score_all_cell_checks\":"
        << last_perf_.score_all_cell_checks << ",";
    out << "\"score_all_evaluated_cell_checks\":"
        << last_perf_.score_all_evaluated_cell_checks;
    out << "}";
    out << "}";
    return out.str();
  }

  void publishSummaryJson(const std::string& json) {
    std_msgs::String msg;
    msg.data = json;
    perf_summary_json_pub_.publish(msg);
  }

  void publishLiveSummary() {
    if (perf_count_ <= 0) return;
    const auto now = std::chrono::steady_clock::now();
    if (has_perf_summary_wall_time_) {
      const auto elapsed =
          std::chrono::duration<double>(now - last_perf_summary_wall_time_);
      if (elapsed.count() < perf_summary_live_period_seconds_) {
        return;
      }
    }
    has_perf_summary_wall_time_ = true;
    last_perf_summary_wall_time_ = now;
    const std::string json = makePerfSummaryJson();
    publishSummaryJson(json);
    writePerfSummaryLog(json);
    ROS_INFO("perf_summary_json=%s", json.c_str());
  }

  void publishFinalSummary() {
    if (perf_count_ <= 0 || summary_printed_) return;
    const std::string json = makePerfSummaryJson();
    publishSummaryJson(json);
    writePerfSummaryLog(json);
    ROS_INFO("perf_summary_json=%s", json.c_str());
    summary_printed_ = true;
  }

  void maybePublishFinalSummaryTimer(const ros::WallTimerEvent&) {
    maybePublishFinalSummary();
  }

  void maybePublishFinalSummary() {
    if (!saw_scan_ || summary_printed_ || perf_count_ <= 0) return;
    const auto idle = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - last_scan_wall_time_);
    if (idle.count() >= perf_summary_idle_seconds_) {
      publishFinalSummary();
    }
  }

  cartographer_parallel::FastMatcher matcher_;
  cartographer_parallel::FastMatcher initial_matcher_;
  cartographer_parallel::FastMatcher initial_refine_matcher_;
  cartographer_parallel::Pose2 pose_;
  cartographer_parallel::Pose2 initial_pose_;
  bool has_pose_ = false;
  bool global_first_match_ = true;
  int global_every_n_ = 0;
  int publish_top_candidates_ = 100;
  int initial_publish_top_candidates_ = 500;
  float initial_min_score_ = 0.90f;
  double perf_summary_idle_seconds_ = 2.0;
  double perf_marker_period_seconds_ = 0.5;
  double perf_summary_live_period_seconds_ = 1.0;
  std::string perf_csv_file_;
  std::string perf_summary_log_file_;
  int scan_count_ = 0;
  std::string map_frame_;
  std::string base_frame_;

  static std::string PerfLine(const PerfStats& perf) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << "perf match=" << perf.match_ms << "ms score_all="
        << perf.score_all_ms << "ms ratio="
        << RatioPct(perf.score_all_ms, perf.match_ms) << "% throughput="
        << ThroughputMChecksPerSec(perf.score_all_cell_checks,
                                   perf.score_all_ms)
        << "Mchecks/s";
    return out.str();
  }

  ros::NodeHandle nh_;
  ros::NodeHandle private_nh_;
  ros::Subscriber scan_sub_;
  ros::Timer map_timer_;
  ros::WallTimer perf_summary_timer_;
  ros::Publisher map_pub_;
  ros::Publisher odom_pub_;
  ros::Publisher candidate_pub_;
  ros::Publisher marker_pub_;
  ros::Publisher candidate_text_pub_;
  ros::Publisher match_ms_pub_;
  ros::Publisher score_all_ms_pub_;
  ros::Publisher match_ms_avg_pub_;
  ros::Publisher score_all_ms_avg_pub_;
  ros::Publisher perf_summary_json_pub_;
  ros::Publisher perf_marker_pub_;
  std::ofstream perf_csv_;
  long long perf_count_ = 0;
  double match_ms_sum_ = 0.0;
  double score_all_ms_sum_ = 0.0;
  double score_sum_ = 0.0;
  long long score_all_calls_sum_ = 0;
  long long score_all_candidates_sum_ = 0;
  long long score_all_evaluated_candidates_sum_ = 0;
  long long score_all_scan_points_sum_ = 0;
  long long score_all_cell_checks_sum_ = 0;
  long long score_all_evaluated_cell_checks_sum_ = 0;
  PerfStats last_perf_;
  bool saw_scan_ = false;
  bool summary_printed_ = false;
  std::chrono::steady_clock::time_point last_scan_wall_time_;
  bool has_perf_marker_wall_time_ = false;
  std::chrono::steady_clock::time_point last_perf_marker_wall_time_;
  bool has_perf_summary_wall_time_ = false;
  std::chrono::steady_clock::time_point last_perf_summary_wall_time_;
};

}  // namespace cartographer_parallel_ros

int main(int argc, char** argv) {
  try {
    ros::init(argc, argv, "fast_correlative_node");
    auto node = std::make_shared<cartographer_parallel_ros::FastCorrelativeNode>();
    (void)node;
    ros::spin();
  } catch (const std::exception& e) {
    ROS_FATAL("%s", e.what());
    ros::shutdown();
    return 1;
  }
  ros::shutdown();
  return 0;
}
