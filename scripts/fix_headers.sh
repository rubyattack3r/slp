#!/bin/bash
for f in include/*.h; do
    sed -i '' 's/^#endif$/#endif \/\/ SLP_H/' "$f"
done
