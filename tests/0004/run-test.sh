#! /bin/bash

. ../test-utils.sh

if [ -f testdb ]; then rm -f testdb; fi

${AUDIODB} -d testdb -N

${AUDIODB} -d testdb -L

intstring 2 > testfeature
floatstring 0 1 >> testfeature
floatstring 1 0 >> testfeature

${AUDIODB} -d testdb -I -f testfeature

echo "query point (0.0,0.5)"
intstring 2 > testquery
floatstring 0 0.5 >> testquery

${AUDIODB} -d testdb -Q point -f testquery > testoutput
echo testfeature 0.5 0 0 > test-expected-output
echo testfeature 0 0 1 >> test-expected-output
cmp testoutput test-expected-output
${AUDIODB} -d testdb -Q point -f testquery -n 1 > testoutput
echo testfeature 0.5 0 0 > test-expected-output
cmp testoutput test-expected-output

echo "query point (0.5,0.0)"
intstring 2 > testquery
floatstring 0.5 0 >> testquery

${AUDIODB} -d testdb -Q point -f testquery > testoutput
echo testfeature 0.5 0 1 > test-expected-output
echo testfeature 0 0 0 >> test-expected-output
cmp testoutput test-expected-output
${AUDIODB} -d testdb -Q point -f testquery -n 1 > testoutput
echo testfeature 0.5 0 1 > test-expected-output
cmp testoutput test-expected-output

exit 104
