#!/usr/bin/env bash
# test_urls.sh
# vim:set ft=shell
#
# set -eou
#
# This script generate URLs so test HTTP server and run requests with `curl`
#
# Just for the record:
# Next line generate huge test file with real words in random order
# shuf -n 100000 /usr/share/dict/words | tr '\n' ' ' > huge.txt
#
# set -xv

# Takes 3 arguments:
#
#   - `file` name to make
#   - `count` bytes in the file
#   - `blocksize` (optional, default=512)
#
# Function will create `file` with `count` bytes made from `/dev/random` with `dd` command.
# Note, that if `blocksize` is greater then `count` actual file size will be `blocksize`.
function make_random_file() {
  local file
  local count
  local blocksize=512

  [ $# -lt 2 ] && echo "make_random_file(): missing 2 (two) arguments" && exit 127
  file=$1
  count=$2
  [ -n "${3:+x}" ] && blocksize=$3

  dd bs=${blocksize} count=$((${count}/${blocksize})) if=/dev/random of=${file} status=progress
}

# Takes 2 arguments:
#
#   - `lines` separated with '\n' (so put this argument in "") and
#   - `number` of required lines in output
#
# Function will take lines and `echo` on stdout exactly `number` of them in random order
function make_random_lines() {
  local lines
  local number
  local lines_number=0
  local out

  [ $# -lt 2 ] && echo "make_random_lines(): missing 2 (two) arguments" && exit 127

  lines="$1"
  number=$2
  lines_number=$(echo "${lines}" | wc -l)
  [ ${lines_number} -lt 1 ] && echo "make_random_lines(): there must be at least one line in 1st argument" && exit 127

  if [ ${number} -le ${lines_number} ]; then
      # sample without replacement
      echo "${lines}" | shuf | head -${number}
  else
      # repeat random shuffle until enough lines
      while [ $(echo "${out}" | wc -l) -lt ${number} ]; do
          # needed=$((${number} - $(echo "${out}" | wc -l)))
          echo "${out}" > tmp_file
          echo "${lines}" >> tmp_file
          out=$(cat tmp_file | shuf)
      done
      rm -f tmp_file
      # Trim exactly to num1 lines
      echo "${out}" | head -${number}
  fi
}

# Takes 3 arguments:
#
#   - `file1` source of lines
#   - `file1` destination of the result
#   - `line_numbers` (optional, default=100)
#
# Function will take `file1`, `grep` lines from it according to the set of words (categories) ['tiny', 'small', 'medium', 'large', 'huge'], and produce output in `file2` exactly with `line_numbers` number of lines. In addition, total number of lines in each category will be distributed by this rule:
#
# tiny=50%, small=20%, medium=10%, large=15%, huge=5%
#
# Also, the result `file2` will be shuffled 5 times, to produce random line order.
function files_with_random_lines() {
  local file1
  local file2
  local line_numbers=100

  [ $# -lt 2 ] && echo "files_with_random_lines: missing 2 (two) arguments" && exit 127

  file1="$1"
  file2="$2"
  [ -n "${3:+x}" ] && line_numbers=$3

  # Collect lines for each category
  tiny_lines=$(grep -i 'tiny' "$file1")
  small_lines=$(grep -i 'small' "$file1")
  medium_lines=$(grep -i 'medium' "$file1")
  large_lines=$(grep -i 'large' "$file1")
  huge_lines=$(grep -i 'huge' "$file1")

  # Counts based on proportions: tiny=50%, small=20%, medium=10%, large=15%, huge=5%
  # Total 100%, target 100 lines for accuracy (adjust as needed)
  target_tiny=$((${line_numbers} * 50 / 100))
  target_small=$((${line_numbers} * 20 / 100))
  target_medium=$((${line_numbers} * 10 / 100))
  target_large=$((${line_numbers} * 15 / 100))
  target_huge=$((${line_numbers} * 5 / 100))

  total_target=$((target_tiny + target_small + target_medium + target_large + target_huge))

  echo "Targets: tiny=$target_tiny, small=$target_small, medium=$target_medium, large=$target_large, huge=$target_huge"

  # Takes 2 arguments:
  #
  #   - `lines` input
  #   - `n` number of result lines
  #
  # Function to pick N random lines from given lines.
  function pick_random() {
    [ $# -lt 2 ] && echo "pick_random(): missing 2 (two) arguments" && exit 127
    local lines="$1"
    local n="$2"
    echo "$lines" | shuf -n ${n}
  }

  # Generate file2
  > "$file2"
  tiny=$(make_random_lines "$tiny_lines" ${target_tiny})
  pick_random "$tiny" "$target_tiny" >> "$file2"
  small=$(make_random_lines "$small_lines" ${target_small})
  pick_random "$small" "$target_small" >> "$file2"
  medium=$(make_random_lines "$medium_lines" ${target_medium})
  pick_random "$medium" "$target_medium" >> "$file2"
  large=$(make_random_lines "$large_lines" ${target_large})
  pick_random "$large" "$target_large" >> "$file2"
  huge=$(make_random_lines "$huge_lines" ${target_huge})
  pick_random "$huge" "$target_huge" >> "$file2"

  # Shuffle the final file2 5 times to mix categories
  for i in $(seq 5); do
    shuf "$file2" > "${file2}.shuffled"
    mv "${file2}.shuffled" "$file2"
  done
}

URLS_FILE=${URLS_FILE:-urls.txt}
LIGHT_PATHS=${LIGHT_PATHS:-light_paths.txt}
FILES_DIR=${FILES_DIR:-files}
SERVER_URL="${SERVER_URL:-http://localhost:8080}"
NUMBER_OF_LINES=${NUMBER_OF_LINES:-100}
PARALLELISM=${PARALLELISM:-100} # How many parallel `curl` processes
LOGS=${LOGS:-./log}

[ -z "${1:+x}" ] && echo "missing parameter -- number of cycles" && exit 127
CNUMBER=$1

# Make big files on the first run
[ -d "${FILES_DIR}" ] || mkdir -p "${FILES_DIR}"
[ -f "${FILES_DIR}"/tiny_1.bin ] || make_random_file "${FILES_DIR}"/tiny_1.bin $((8)) 1
[ -f "${FILES_DIR}"/tiny_2.bin ] || make_random_file "${FILES_DIR}"/tiny_2.bin $((64)) 8
[ -f "${FILES_DIR}"/tiny_3.bin ] || make_random_file "${FILES_DIR}"/tiny_3.bin $((128)) 8
[ -f "${FILES_DIR}"/small_1K.bin ] || make_random_file "${FILES_DIR}"/small_1K.bin $((1*1024))
[ -f "${FILES_DIR}"/small_10K.bin ] || make_random_file "${FILES_DIR}"/small_10K.bin $((10*1024))
[ -f "${FILES_DIR}"/medium_100K.bin ] || make_random_file "${FILES_DIR}"/medium_100K.bin $((100*1024))
[ -f "${FILES_DIR}"/large_500K.bin ] || make_random_file "${FILES_DIR}"/large_500K.bin $((500*1024))
[ -f "${FILES_DIR}"/huge_1M.bin ] || make_random_file "${FILES_DIR}"/huge_1M.bin $((1*1024*1024))
[ -f "${FILES_DIR}"/huge_2M.bin ] || make_random_file "${FILES_DIR}"/huge_2M.bin $((2*1024*1024))
[ -f "${FILES_DIR}"/huge_3M.bin ] || make_random_file "${FILES_DIR}"/huge_3M.bin $((3*1024*1024))
[ -f "${FILES_DIR}"/huge_4M.bin ] || make_random_file "${FILES_DIR}"/huge_4M.bin $((4*1024*1024))
[ -f "${FILES_DIR}"/huge_5M.bin ] || make_random_file "${FILES_DIR}"/huge_5M.bin $((5*1024*1024))

>"${URLS_FILE}"
>"${URLS_FILE}1.tmp"
>"${URLS_FILE}2.tmp"

for file in $(cd "${FILES_DIR}" && echo *); do
  echo "${SERVER_URL}/${FILES_DIR}/${file}" >> "${URLS_FILE}1.tmp"
done

for path in $(cat ${LIGHT_PATHS}); do
  echo ${SERVER_URL}${path} >> "${URLS_FILE}2.tmp"
done

files_with_random_lines "${URLS_FILE}1.tmp" "${URLS_FILE}" ${NUMBER_OF_LINES}
make_random_lines "$(cat ${URLS_FILE}2.tmp)" ${NUMBER_OF_LINES} >> "${URLS_FILE}"
rm -f ${URLS_FILE}1.tmp ${URLS_FILE}2.tmp

# Shuffle the final URLS_FILE 5 times to mix categories, exclude empty lines
for i in $(seq 5); do
  shuf "${URLS_FILE}" | grep -Ev '^$' > "${URLS_FILE}.shuffled"
  mv "${URLS_FILE}.shuffled" "${URLS_FILE}"
done

# Function to take `PARALLELISM` lines from file on each `take`
function take_lines() {
  local file=$1
  local take=$2
  local tail=$(($(cat ${file} | wc -l) - ${take}*${PARALLELISM}))

  tail -${tail} ${file} | head -${PARALLELISM}
}

lines=$(cat "${URLS_FILE}" | wc -l)
test_chunks=$((${lines} / ${PARALLELISM}))
test_chunk_mod=$((${lines} % ${PARALLELISM}))
# echo $lines " " $PARALLELISM " " $test_chunks " " $test_chunk_mod
# exit

[ -d "${LOGS}" ] || mkdir -p "${LOGS}"

# Make `CNUMBER` equal cycles
for i in $(seq ${CNUMBER}); do
  for take in $(seq 0 ${test_chunks}); do
    echo "Take ${take} of ${i} cycle. "
    urls=$(take_lines "${URLS_FILE}" ${take})
    [ -z "${urls}" ] && break
    n=0
    for url in $(echo "${urls}"); do
      curl -v $url >/dev/null 2>${LOGS}/curl-${i}-${take}-${n}.log &
      n=$((n + 1))
    done
    # sleep 0.3
  done
done

sleep 30
