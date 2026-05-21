#!/bin/bash
for f in include/*.h; do
    sed -i '' 's/^#endif$/#endif \/\/ SLEEPY_H/' "$f"
done
