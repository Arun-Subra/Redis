#!/usr/bin/env bash

NCLIENTS=10
NREQ=10
OUTFILE="output.txt"

> "$OUTFILE"  # Clear previous output

for id in $(seq 1 $NCLIENTS); do
  (
    for n in $(seq 1 $NREQ); do
      # Combine key and value in one atomic write
      result=$(./client get "k${id}_${n}")
      echo "k${id}_${n}: $result" >> "$OUTFILE"
    done
  ) &
done

wait
echo "ALL READS DONE, see $OUTFILE"
