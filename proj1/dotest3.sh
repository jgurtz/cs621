#!/bin/bash

INIT="./jvol -c init -f testfile"
MK_DIR="./jvol -f testfile -c mkdir -p "
TOUCH_FILE="./jvol -f testfile -c touch -p "
RM_DIR="./jvol -f testfile -c rm -p "

for i in {1..2}
do
    eval "$MK_DIR" dir${i}

    for j in {1..35}
    do
        eval "$TOUCH_FILE" file${i}_${j}
    done
done

