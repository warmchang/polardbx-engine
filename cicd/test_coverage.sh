#!/usr/bin/bash
source cicd/common.sh

if [ "${TEST_TYPE_ENUM}" -ne "${MERGE_TEST_COVERAGE}" ] || 
   [ "${TEST_TYPE_ENUM}" -ne "${DAILY_REGRESSION}" ]; then
  exit 0
fi

if [ "${TEST_TYPE_ENUM}" -eq "${MERGE_TEST_COVERAGE}"]; then
  echo "MERGE_ID: ${MERGE_ID}"
  MERGE_COMMITS=$(curl -k -s "https://code.aone.alibaba-inc.com/api/v4/projects/2694395/merge_request/${MERGE_ID}/push_records" | jq -r jq -r '.[] | {disappear_from_push_id: .commits[0].disappear_from_push_id, new_revision: .new_revision}')
  LAST_COMMID_ID=$(git log -n 1 --format=%H)
  LAST_COMMIT_TIME=$(git log -1 --format=%cd --date=format:'%Y-%m-%d %H:%M:%S')
  LAST_COMMIT_EMAIL=$(git log -1 --pretty=format:'%ce')

  cp cicd/cc_gen.sh ${CICD_BUILD_ROOT}
  chmod +x ${CICD_BUILD_ROOT}/cc_gen.sh

  cd ${CICD_BUILD_ROOT}
  ./cc_gen.sh -c "${FIRST_COMMIT_ID}"

else 
  ./cc_gen.sh
fi

# PUSH RESULT
result=$(
  cat <<EOF
'{
  "emp_email": "${LAST_COMMIT_EMAIL}",
  "source_branch": "${SOURCE_BRANCH}",
  "target_branch": "${TARGET_BRANCH}",
  "cr_last_commit_id": "${LAST_COMMID_ID}",
  "commit_time": "${LAST_COMMIT_TIME}",
  "incremental_coverage_rate": "${CODE_COVERAGE_DELTA}"
}'
EOF
)

echo "curl -X PUT -H \"Content-Type: application/json\" -d ${result} https://qingzhou.aliyun-inc.com:5199/restapi/aliyun/git_code_coverage"

max_retries=6
retry_interval=10

attempt=1
while true; do
  response=$(curl -s -o /dev/null -w "%{http_code}" -X PUT -H "Content-Type: application/json" -d "${result}" https://qingzhou.aliyun-inc.com:5199/restapi/aliyun/git_code_coverage)

  if [[ $response =~ ^2 ]]; then
    echo "put delta success."
    break
  else
    echo "put delta failed, response: $response"
  fi

  if [ $attempt -ge $max_retries ]; then
    echo "retry up to $max_retries times"
    exit 1
  fi

  echo "wait $retry_interval seconds to retry... (attempt: $attempt)"
  sleep $retry_interval

  ((attempt++))
done
