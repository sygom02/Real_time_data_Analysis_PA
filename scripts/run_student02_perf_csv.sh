#!/usr/bin/env bash
set -euo pipefail

NS="${NS:-student02}"
BACKEND="${BACKEND:-cpu_baseline}"
WORKSPACE="${WORKSPACE:-/root/catkin_ws}"
PACKAGE="${PACKAGE:-cartographer_parallel}"
PACKAGE_DIR="${PACKAGE_DIR:-${WORKSPACE}/src/cartographer_parallel/cartographer_parallel}"
BAG_FILE="${BAG_FILE:-${PACKAGE_DIR}/bags/scan.bag}"
CSV_FILE="${CSV_FILE:-/tmp/fast_correlative_perf_${NS}.csv}"
SUMMARY_LOG_FILE="${SUMMARY_LOG_FILE:-/tmp/fast_correlative_summary_${NS}.log}"
BUILD_CUDA_TASK="${BUILD_CUDA_TASK:-ON}"

if [ -f /opt/ros/melodic/setup.bash ]; then
  source /opt/ros/melodic/setup.bash
fi

cd "${WORKSPACE}"
catkin_make -DCMAKE_BUILD_TYPE=Release -DBUILD_CUDA_TASK="${BUILD_CUDA_TASK}"
source "${WORKSPACE}/devel/setup.bash"

rm -f "${CSV_FILE}"
rm -f "${SUMMARY_LOG_FILE}"

echo "namespace: ${NS}"
echo "backend:   ${BACKEND}"
echo "bag:       ${BAG_FILE}"
echo "csv:       ${CSV_FILE}"
echo "summary:   ${SUMMARY_LOG_FILE}"

roslaunch "${PACKAGE}" cartographer_parallel_with_bag.launch \
  ns:="${NS}" \
  use_bag:=true \
  bag_file:="${BAG_FILE}" \
  score_all_backend:="${BACKEND}" \
  perf_csv_file:="${CSV_FILE}" \
  perf_summary_log_file:="${SUMMARY_LOG_FILE}"

echo
echo "CSV result:"
if [ -s "${CSV_FILE}" ]; then
  ls -l "${CSV_FILE}"
  echo
  echo "First rows:"
  head -5 "${CSV_FILE}"
  echo
  echo "Last rows:"
  tail -5 "${CSV_FILE}"
else
  echo "CSV was not created or is empty: ${CSV_FILE}"
  echo "Check whether fast_correlative_node started and subscribed to /${NS}/scan."
fi

echo
echo "Summary log:"
if [ -s "${SUMMARY_LOG_FILE}" ]; then
  ls -l "${SUMMARY_LOG_FILE}"
  tail -5 "${SUMMARY_LOG_FILE}"
else
  echo "Summary log was not created or is empty: ${SUMMARY_LOG_FILE}"
  echo "The /${NS}/perf/summary_json topic should publish after input scans stop for perf_summary_idle_seconds."
fi
