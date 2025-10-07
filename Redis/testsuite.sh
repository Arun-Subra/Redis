#!/bin/bash

echo "====== Starting Redis-like Command Tests ======"

echo -e "\n-- Test: SET commands --"
echo "Command: ./client set foo bar"
echo "Expected: OK or success message"
./client set foo bar

echo "Command: ./client set empty \"\""
echo "Expected: OK (empty string value)"
./client set empty ""

echo "Command: ./client set number 123"
echo "Expected: OK"
./client set number 123

echo -e "\n-- Test: GET commands --"
echo "Command: ./client get foo"
echo "Expected: bar"
./client get foo

echo "Command: ./client get empty"
echo "Expected: (empty string)"
./client get empty

echo "Command: ./client get number"
echo "Expected: 123"
./client get number

echo "Command: ./client get missing"
echo "Expected: nil or error"
./client get missing

echo -e "\n-- Test: DEL commands --"
echo "Command: ./client del foo"
echo "Expected: OK"
./client del foo

echo "Command: ./client get foo"
echo "Expected: nil or error"
./client get foo

echo "Command: ./client del missing"
echo "Expected: no error (graceful handling of non-existent key)"
./client del missing

echo -e "\n-- Test: ZADD to 'scores' zset --"
echo "Command: ./client zadd scores 10 alice"
echo "Expected: OK"
./client zadd scores 10 alice

echo "Command: ./client zadd scores 20 bob"
./client zadd scores 20 bob

echo "Command: ./client zadd scores 15 charlie"
./client zadd scores 15 charlie

echo "Command: ./client zadd scores 20 bob"
echo "Expected: No change (duplicate member and score)"
./client zadd scores 20 bob

echo "Command: ./client zadd scores 25 dave"
./client zadd scores 25 dave

echo "Command: ./client zadd scores -5 eve"
echo "Expected: OK (negative score)"
./client zadd scores -5 eve

echo "Command: ./client zadd scores 0 frank"
echo "Expected: OK (zero score)"
./client zadd scores 0 frank

echo -e "\n-- Test: ZCARD after ZADD --"
echo "Command: ./client zcard scores"
echo "Expected: 6"
./client zcard scores

echo -e "\n-- Test: ZRANGE full range --"
echo "Command: ./client zrange scores 0 -1"
echo "Expected: eve (lowest) to dave (highest), total 6"
./client zrange scores 0 -1

echo -e "\n-- Test: ZRANGE partial range --"
echo "Command: ./client zrange scores 0 2"
echo "Expected: 3 lowest scoring members"
./client zrange scores 0 2

echo "Command: ./client zrange scores 5 10"
echo "Expected: Only last (highest) member or empty if out of bounds"
./client zrange scores 5 10

echo "Command: ./client zrange scores 2 1"
echo "Expected: Empty (start > stop)"
./client zrange scores 2 1

echo -e "\n-- Test: ZSCORE --"
echo "Command: ./client zscore scores alice"
echo "Expected: 10"
./client zscore scores alice

echo "Command: ./client zscore scores bob"
echo "Expected: 20"
./client zscore scores bob

echo "Command: ./client zscore scores unknown"
echo "Expected: nil or error"
./client zscore scores unknown

echo -e "\n-- Test: ZREM --"
echo "Command: ./client zrem scores alice"
echo "Expected: OK"
./client zrem scores alice

echo "Command: ./client zrem scores unknown"
echo "Expected: graceful handling"
./client zrem scores unknown

echo "Command: ./client zrange scores 0 -1"
echo "Expected: alice removed"
./client zrange scores 0 -1

echo -e "\n-- Test: ZCARD after ZREM --"
echo "Command: ./client zcard scores"
echo "Expected: 5"
./client zcard scores

echo -e "\n-- Test: SET + DEL mixed --"
echo "Command: ./client set myset active"
./client set myset active

echo "Command: ./client get myset"
echo "Expected: active"
./client get myset

echo "Command: ./client del myset"
./client del myset

echo "Command: ./client get myset"
echo "Expected: nil or error"
./client get myset

echo -e "\n-- Test: Reuse zset key after deletion --"
echo "Command: ./client del scores"
./client del scores

echo "Command: ./client zcard scores"
echo "Expected: 0 or error"
./client zcard scores

echo "Command: ./client zadd scores 100 newguy"
./client zadd scores 100 newguy

echo "Command: ./client zrange scores 0 -1"
echo "Expected: newguy"
./client zrange scores 0 -1

echo -e "\n====== All Tests Completed Successfully ======"
