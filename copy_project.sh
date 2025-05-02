#!/bin/bash

# Output file
OUTPUT_FILE="project_copy.txt"

# Add Makefile
echo "===== Makefile =====" >> "$OUTPUT_FILE"
cat Makefile >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# Add all .h files
for header in inc/*.h; do
    echo "===== $header =====" >> "$OUTPUT_FILE"
    cat "$header" >> "$OUTPUT_FILE"
    echo -e "\n\n" >> "$OUTPUT_FILE"
done

for header in browser/inc/*.h; do
    echo "===== $header =====" >> "$OUTPUT_FILE"
    cat "$header" >> "$OUTPUT_FILE"
    echo -e "\n\n" >> "$OUTPUT_FILE"
done


# Add all .c files
for source in src/*.c; do
    echo "===== $source =====" >> "$OUTPUT_FILE"
    cat "$source" >> "$OUTPUT_FILE"
    echo -e "\n\n" >> "$OUTPUT_FILE"
done
for source in browser/src/*.c; do
    echo "===== $source =====" >> "$OUTPUT_FILE"
    cat "$source" >> "$OUTPUT_FILE"
    echo -e "\n\n" >> "$OUTPUT_FILE"
done

echo "Files copied to $OUTPUT_FILE"

