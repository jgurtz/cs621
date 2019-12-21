#!/bin/bash

INIT="./jvol -c init -f testfile"
MK_DIR="./jvol -f testfile -c mkdir -p "
RM_DIR="./jvol -f testfile -c rm -p "

for i in {1..97}
do
    eval "$MK_DIR" dir${i}
done

