#! /bin/bash

. ../test-utils.sh

if [ -f testdb ]; then rm -f testdb; fi

${AUDIODB} -d testdb -N

intstring 2 > testfeature
floatstring 0 1 >> testfeature
floatstring 1 0 >> testfeature

${AUDIODB} -d testdb -I -f testfeature

# sequence queries require L2NORM
${AUDIODB} -d testdb -L

echo "query point (0.0,0.5)"
intstring 2 > testquery
floatstring 0 0.5 >> testquery

${AUDIODB} -d testdb -Q sequence -l 1 -f testquery > testoutput
echo testfeature 1 0 0 > test-expected-output
cmp testoutput test-expected-output
${AUDIODB} -d testdb -Q sequence -l 1 -f testquery -n 1 > testoutput
echo testfeature 0 0 0 > test-expected-output
cmp testoutput test-expected-output

# test query point past the end of query track
expect_clean_error_exit ${AUDIODB} -d testdb -Q sequence -l 1 -f testquery -p 1


echo "query point (0.5,0.0)"
intstring 2 > testquery
floatstring 0.5 0 >> testquery

${AUDIODB} -d testdb -Q sequence -l 1 -f testquery > testoutput
echo testfeature 1 0 1 > test-expected-output
cmp testoutput test-expected-output
${AUDIODB} -d testdb -Q sequence -l 1 -f testquery -n 1 > testoutput
echo testfeature 0 0 1 > test-expected-output
cmp testoutput test-expected-output

exit 104
