#!/bin/sh
# Rebuild docs/demo.html (the GitHub Pages demo) from the device page plus a mock API.
#   tools/make-demo.sh
set -e
cd "$(dirname "$0")/.."
sed -e 's|<title>pianolog</title>|<title>pianolog · demo with example takes</title>|' \
    -e 's|<script>|<script src="demo-shim.js"></script>\n<script>|' \
    main/www/index.html > docs/demo.html
cp main/www/fonts/*.woff2 docs/fonts/
echo "docs/demo.html rebuilt"
