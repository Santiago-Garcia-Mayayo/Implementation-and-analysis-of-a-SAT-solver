#!/bin/bash

for file in ./files/*; do
    echo "Running: $file"
    ./main "$file"
    
    status=$?
    if [ $status -ne 0 ]; then
        echo "Error (exit code $status) with file: $file"
        read -p "Press Enter to continue to the next file..."  # optional pause
    fi
done
