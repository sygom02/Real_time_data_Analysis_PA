#!/usr/bin/env bash
set -euo pipefail

NS="${NS:-student02}"
CSV_FILE="${CSV_FILE:-/tmp/fast_correlative_perf_${NS}.csv}"
SUMMARY_LOG_FILE="${SUMMARY_LOG_FILE:-/tmp/fast_correlative_summary_${NS}.log}"

echo "csv: ${CSV_FILE}"
echo "summary: ${SUMMARY_LOG_FILE}"

if [ ! -f "${CSV_FILE}" ]; then
  echo "missing"
  exit 1
fi

ls -l "${CSV_FILE}"
echo
echo "Header:"
head -1 "${CSV_FILE}"
echo
echo "Event counts:"
awk -F, 'NR > 1 { count[$1]++ } END { for (event in count) print event, count[event] }' "${CSV_FILE}" | sort
echo
echo "First data rows:"
sed -n '1,6p' "${CSV_FILE}"
echo
echo "Last data rows:"
tail -5 "${CSV_FILE}"
echo
echo "Rows with matching metrics:"
grep -n "initial_match\\|match" "${CSV_FILE}" | head || true

echo
echo "Summary log:"
if [ -f "${SUMMARY_LOG_FILE}" ]; then
  ls -l "${SUMMARY_LOG_FILE}"
  tail -5 "${SUMMARY_LOG_FILE}"
else
  echo "missing"
fi
