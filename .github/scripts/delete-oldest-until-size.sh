#!/bin/bash
# This script is used by CI to delete files from a cache folder.

if [[ $# -ne 2 ]]; then
  echo "Expected 2 parameters"
  exit 1
fi

DIR_PATH="$1"
MAX_DIR_SIZE="$2"

if [ ! -d "$DIR_PATH" ]; then
  echo "The given directory $DIR_PATH does not exist. Exiting"
  exit 0
fi

CURRENT_SIZE=$(du -b -d 0 "$DIR_PATH" | cut -f 1)

function toBytes() {
	numfmt --to=iec --suffix=B $1
}

echo "Directory size is $(toBytes $CURRENT_SIZE); limit is $(toBytes $MAX_DIR_SIZE)."

# Get all file paths ordered oldest first
readarray -t FILES < <(find $DIR_PATH -type f -printf "%A@ %p\n" | sort | cut -f 2 -d " ")
FILE_COUNT=${#FILES[@]}
echo "Directory contains $FILE_COUNT file(s)."

# Delete files oldest first until the folder size doesn't exceed the limit,
# or until we run out of files.
i=0
while [[ $CURRENT_SIZE -gt $MAX_DIR_SIZE && $i -lt $FILE_COUNT ]]
do
	FILE_TO_DELETE=${FILES[$i]}
	FILE_SIZE=$(du -b $FILE_TO_DELETE | cut -f 1)
	rm $FILE_TO_DELETE
	CURRENT_SIZE=$((CURRENT_SIZE - FILE_SIZE))
	i=$((i + 1))
done

echo "Deleted $i file(s). Directory size is now $(toBytes $CURRENT_SIZE)."

