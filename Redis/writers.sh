#!/usr/bin/env bash

NCLIENTS=100
NREQ=100

for id in $(seq 1 $NCLIENTS); do
  (
    for n in $(seq 1 $NREQ); do
      ./client set "k${id}_${n}" "v${id}_${n}" >/dev/null
    done
  ) &
done

wait
echo "ALL WRITES DONE"
