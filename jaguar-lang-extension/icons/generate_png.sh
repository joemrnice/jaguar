#!/bin/bash
# Convert SVG to PNG using rsvg-convert or ImageMagick
if command -v rsvg-convert &> /dev/null; then
    rsvg-convert -w 128 -h 128 icons/jag.svg -o icons/jag.png
    echo "PNG generated with rsvg-convert"
elif command -v convert &> /dev/null; then
    convert -background none -resize 128x128 icons/jag.svg icons/jag.png
    echo "PNG generated with ImageMagick"
else
    echo "No converter found. Please install librsvg or imagemagick."
    exit 1
fi
