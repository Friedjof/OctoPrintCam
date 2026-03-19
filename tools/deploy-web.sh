#!/bin/bash

# ESP32 Web Interface Deployment Script
# Copies and optimizes web files from /web/dist/ to /data-template/ for ESP32 deployment

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE_DIR="${REPO_ROOT}/web/dist"
TARGET_DIR="${REPO_ROOT}/data-template"
MAX_SIZE_KB=150
TEMP_DIR=$(mktemp -d)

echo -e "${BLUE}ESP32 Web Interface Deployment${NC}"
echo "============================================"

# Check if source directory exists
if [ ! -d "$SOURCE_DIR" ]; then
    echo -e "${RED}❌ Error: Source directory '$SOURCE_DIR' not found${NC}"
    echo -e "${YELLOW}Run 'cd web && make build' first${NC}"
    exit 1
fi

# Create target directory if needed
if [ ! -d "$TARGET_DIR" ]; then
    echo -e "${YELLOW}⚠️  Creating target directory '$TARGET_DIR'${NC}"
    mkdir -p "$TARGET_DIR"
fi

# Backup existing config.json if it exists
CONFIG_BACKUP=""
if [ -f "$TARGET_DIR/config.json" ]; then
    CONFIG_BACKUP="$TEMP_DIR/config.json.backup"
    cp "$TARGET_DIR/config.json" "$CONFIG_BACKUP"
    echo -e "${YELLOW}📦 Backed up existing config.json${NC}"
fi

echo -e "${BLUE}🚀 Starting deployment...${NC}"

# Clean target directory (except config.json)
echo -e "${YELLOW}🧹 Cleaning target directory...${NC}"
find "$TARGET_DIR" -type f -not -name "config.json" -delete 2>/dev/null || true
find "$TARGET_DIR" -type d -empty -delete 2>/dev/null || true

# Copy web files
echo -e "${YELLOW}📂 Copying built files...${NC}"
cp -r "$SOURCE_DIR"/* "$TARGET_DIR/" 2>/dev/null || true

# Restore config.json if it was backed up
if [ -n "$CONFIG_BACKUP" ] && [ -f "$CONFIG_BACKUP" ]; then
    cp "$CONFIG_BACKUP" "$TARGET_DIR/config.json"
    echo -e "${GREEN}✅ Restored config.json backup${NC}"
fi

# Calculate total size
total_size_bytes=$(find "$TARGET_DIR" -type f -exec stat -f%z {} \; 2>/dev/null | awk '{sum+=$1} END {print sum}' || \
                   find "$TARGET_DIR" -type f -exec stat -c%s {} \; 2>/dev/null | awk '{sum+=$1} END {print sum}')
total_size_kb=$((total_size_bytes / 1024))

echo ""
echo -e "${BLUE}📊 Deployment Summary${NC}"
echo "====================="

# List deployed files with sizes
echo -e "${YELLOW}Deployed files:${NC}"
find "$TARGET_DIR" -type f | while read -r file; do
    size=$(stat -f%z "$file" 2>/dev/null || stat -c%s "$file" 2>/dev/null)
    size_kb=$((size / 1024))
    if [ $size_kb -eq 0 ]; then
        size_kb="<1"
    fi
    rel_path=$(realpath --relative-to="$TARGET_DIR" "$file" 2>/dev/null || basename "$file")
    echo "  ${rel_path}: ${size_kb}kB"
done

echo ""
echo -e "${YELLOW}Total size: ${total_size_kb}kB${NC}"

# Check size constraint
if [ $total_size_kb -gt $MAX_SIZE_KB ]; then
    echo -e "${RED}❌ Warning: Total size (${total_size_kb}kB) exceeds ESP32 limit (${MAX_SIZE_KB}kB)${NC}"
    echo -e "${YELLOW}Consider removing assets or further optimization${NC}"
    exit 1
else
    echo -e "${GREEN}✅ Size check passed (${total_size_kb}kB ≤ ${MAX_SIZE_KB}kB)${NC}"
fi

# Generate file manifest
echo -e "${BLUE}📋 Generating file manifest...${NC}"
manifest_file="$TARGET_DIR/manifest.json"
cat > "$manifest_file" << EOF
{
  "generated": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
  "total_size_kb": $total_size_kb,
  "max_size_kb": $MAX_SIZE_KB,
  "files": [
EOF

first=true
find "$TARGET_DIR" -type f -not -name "manifest.json" | while read -r file; do
    if [ "$first" = true ]; then
        first=false
    else
        echo "," >> "$manifest_file"
    fi

    filename=$(basename "$file")
    size=$(stat -f%z "$file" 2>/dev/null || stat -c%s "$file" 2>/dev/null)

    echo -n "    {\"name\": \"$filename\", \"size\": $size}" >> "$manifest_file"
done

cat >> "$manifest_file" << EOF

  ]
}
EOF

echo -e "${GREEN}✅ Generated manifest.json${NC}"

# Cleanup
rm -rf "$TEMP_DIR"

echo ""
echo -e "${GREEN}🎉 Deployment completed successfully!${NC}"
echo ""
echo -e "${BLUE}Next steps:${NC}"
echo "1. Upload filesystem: ${YELLOW}pio run --target uploadfs${NC}"
echo "2. Or use web-to-header.py: ${YELLOW}cd web && make build-esp${NC}"
echo ""
echo -e "${BLUE}Files deployed to: ${YELLOW}$TARGET_DIR/${NC}"
echo -e "${BLUE}Total size: ${YELLOW}${total_size_kb}kB${NC} (${GREEN}$(($MAX_SIZE_KB - $total_size_kb))kB remaining${NC})"
