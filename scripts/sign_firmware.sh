#!/bin/bash
#
# Firmware Signing Script for TinyBMS Gateway
#
# This script signs firmware binaries with RSA private key for secure OTA updates.
#
# Usage:
#   ./sign_firmware.sh <firmware.bin> [private_key.pem]
#
# Examples:
#   ./sign_firmware.sh build/tinybms-gw.bin
#   ./sign_firmware.sh build/tinybms-gw.bin path/to/ota_private_key.pem
#

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default paths
DEFAULT_PRIVATE_KEY="main/ota_update/keys/ota_private_key.pem"

# Parse arguments
FIRMWARE_FILE="$1"
PRIVATE_KEY="${2:-$DEFAULT_PRIVATE_KEY}"
SIGNATURE_FILE="${FIRMWARE_FILE}.sig"

# Usage
if [ $# -lt 1 ]; then
    echo "Usage: $0 <firmware.bin> [private_key.pem]"
    echo ""
    echo "Examples:"
    echo "  $0 build/tinybms-gw.bin"
    echo "  $0 build/tinybms-gw.bin path/to/ota_private_key.pem"
    echo ""
    echo "Default private key: $DEFAULT_PRIVATE_KEY"
    exit 1
fi

# Check dependencies
if ! command -v openssl &> /dev/null; then
    echo -e "${RED}ERROR: OpenSSL is not installed${NC}"
    echo "Please install OpenSSL:"
    echo "  - Ubuntu/Debian: sudo apt-get install openssl"
    echo "  - macOS: brew install openssl"
    exit 1
fi

echo -e "${BLUE}=== TinyBMS Gateway Firmware Signer ===${NC}"
echo ""

# Verify firmware file exists
if [ ! -f "$FIRMWARE_FILE" ]; then
    echo -e "${RED}ERROR: Firmware file not found: $FIRMWARE_FILE${NC}"
    exit 1
fi

# Verify private key exists
if [ ! -f "$PRIVATE_KEY" ]; then
    echo -e "${RED}ERROR: Private key not found: $PRIVATE_KEY${NC}"
    echo ""
    echo "Generate keys first:"
    echo "  cd main/ota_update/keys"
    echo "  openssl genrsa -out ota_private_key.pem 2048"
    echo "  openssl rsa -in ota_private_key.pem -pubout -out ota_public_key.pem"
    echo ""
    echo "See main/ota_update/keys/README.md for details"
    exit 1
fi

# Get file sizes
FIRMWARE_SIZE=$(stat -f%z "$FIRMWARE_FILE" 2>/dev/null || stat -c%s "$FIRMWARE_FILE" 2>/dev/null)

echo "Configuration:"
echo "  Firmware:    $FIRMWARE_FILE ($FIRMWARE_SIZE bytes)"
echo "  Private Key: $PRIVATE_KEY"
echo "  Output:      $SIGNATURE_FILE"
echo ""

# Verify private key format
echo -e "${GREEN}Step 1/4: Verifying private key...${NC}"
if ! openssl rsa -in "$PRIVATE_KEY" -check -noout 2>/dev/null; then
    echo -e "${RED}✗ Invalid private key format${NC}"
    exit 1
fi

KEY_BITS=$(openssl rsa -in "$PRIVATE_KEY" -text -noout 2>/dev/null | grep "Private-Key:" | awk '{print $2}' | tr -d '()')
echo -e "${GREEN}✓ Valid RSA private key ($KEY_BITS bit)${NC}"

# Compute firmware hash
echo ""
echo -e "${GREEN}Step 2/4: Computing SHA-256 hash of firmware...${NC}"
HASH=$(openssl dgst -sha256 "$FIRMWARE_FILE" | awk '{print $2}')
echo "  SHA-256: $HASH"

# Sign firmware
echo ""
echo -e "${GREEN}Step 3/4: Signing firmware with private key...${NC}"
if openssl dgst -sha256 -sign "$PRIVATE_KEY" -out "$SIGNATURE_FILE" "$FIRMWARE_FILE" 2>/dev/null; then
    SIGNATURE_SIZE=$(stat -f%z "$SIGNATURE_FILE" 2>/dev/null || stat -c%s "$SIGNATURE_FILE" 2>/dev/null)
    echo -e "${GREEN}✓ Signature created: $SIGNATURE_FILE ($SIGNATURE_SIZE bytes)${NC}"
else
    echo -e "${RED}✗ Failed to create signature${NC}"
    exit 1
fi

# Verify signature (optional but recommended)
echo ""
echo -e "${GREEN}Step 4/4: Verifying signature...${NC}"

# Extract public key from private key
PUBLIC_KEY_TEMP=$(mktemp)
openssl rsa -in "$PRIVATE_KEY" -pubout -out "$PUBLIC_KEY_TEMP" 2>/dev/null

# Verify
HASH_TEMP=$(mktemp)
openssl dgst -sha256 -binary "$FIRMWARE_FILE" > "$HASH_TEMP"

if openssl rsautl -verify -inkey "$PUBLIC_KEY_TEMP" -pubin -in "$SIGNATURE_FILE" 2>/dev/null | \
   diff -q - "$HASH_TEMP" > /dev/null 2>&1; then
    echo -e "${GREEN}✓ Signature verification successful${NC}"
else
    echo -e "${RED}✗ Signature verification failed!${NC}"
    rm -f "$PUBLIC_KEY_TEMP" "$HASH_TEMP"
    exit 1
fi

# Cleanup temp files
rm -f "$PUBLIC_KEY_TEMP" "$HASH_TEMP"

echo ""
echo -e "${GREEN}=== Signing Complete ===${NC}"
echo ""
echo "Generated files:"
echo "  ✓ $FIRMWARE_FILE"
echo "  ✓ $SIGNATURE_FILE"
echo ""
echo "Upload both files to gateway for OTA update:"
echo ""
echo -e "${YELLOW}curl -u admin:password -F \"firmware=@$FIRMWARE_FILE\" \\"
echo -e "     -F \"signature=@$SIGNATURE_FILE\" \\"
echo -e "     -H \"X-CSRF-Token: \$TOKEN\" \\"
echo -e "     https://gateway-ip/api/ota${NC}"
echo ""
echo "⚠️  IMPORTANT: Keep $PRIVATE_KEY SECRET and SECURE!"
echo ""
echo -e "${GREEN}Done!${NC}"
