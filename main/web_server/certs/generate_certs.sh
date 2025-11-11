#!/bin/bash
#
# Certificate Generation Script for TinyBMS Gateway
#
# This script generates self-signed SSL/TLS certificates for HTTPS support.
#
# Usage:
#   ./generate_certs.sh [hostname] [validity_days]
#
# Examples:
#   ./generate_certs.sh                          # Use defaults
#   ./generate_certs.sh tinybms-gw.local        # Custom hostname
#   ./generate_certs.sh tinybms-gw.local 3650   # Custom hostname + 10 years validity
#

set -e  # Exit on error

# Configuration
DEFAULT_HOSTNAME="tinybms-gw.local"
DEFAULT_VALIDITY=3650  # 10 years
KEY_SIZE=2048

HOSTNAME="${1:-$DEFAULT_HOSTNAME}"
VALIDITY="${2:-$DEFAULT_VALIDITY}"

CERT_FILE="server_cert.pem"
KEY_FILE="server_key.pem"
DER_FILE="server_cert.der"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if OpenSSL is installed
if ! command -v openssl &> /dev/null; then
    echo -e "${RED}ERROR: OpenSSL is not installed${NC}"
    echo "Please install OpenSSL:"
    echo "  - Ubuntu/Debian: sudo apt-get install openssl"
    echo "  - macOS: brew install openssl"
    echo "  - Windows: Download from https://slproweb.com/products/Win32OpenSSL.html"
    exit 1
fi

echo -e "${GREEN}=== TinyBMS Gateway Certificate Generator ===${NC}"
echo ""
echo "Configuration:"
echo "  Hostname: $HOSTNAME"
echo "  Validity: $VALIDITY days (~$(($VALIDITY / 365)) years)"
echo "  Key Size: $KEY_SIZE bits RSA"
echo "  Output:   $CERT_FILE, $KEY_FILE"
echo ""

# Warn if overwriting existing files
if [ -f "$CERT_FILE" ] || [ -f "$KEY_FILE" ]; then
    echo -e "${YELLOW}WARNING: Existing certificate files will be overwritten!${NC}"
    read -p "Continue? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Aborted."
        exit 1
    fi
fi

echo -e "${GREEN}Step 1/3: Generating private key...${NC}"
openssl genrsa -out "$KEY_FILE" $KEY_SIZE 2>/dev/null

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Private key generated: $KEY_FILE${NC}"
else
    echo -e "${RED}✗ Failed to generate private key${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}Step 2/3: Generating self-signed certificate...${NC}"

# Create certificate with Subject Alternative Names for better browser compatibility
openssl req -new -x509 \
    -key "$KEY_FILE" \
    -out "$CERT_FILE" \
    -days $VALIDITY \
    -subj "/C=XX/ST=State/L=City/O=TinyBMS/OU=Gateway/CN=$HOSTNAME" \
    -addext "subjectAltName=DNS:$HOSTNAME,DNS:*.local,IP:192.168.1.1" \
    2>/dev/null

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Certificate generated: $CERT_FILE${NC}"
else
    echo -e "${RED}✗ Failed to generate certificate${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}Step 3/3: Generating DER format (optional)...${NC}"
openssl x509 -in "$CERT_FILE" -outform DER -out "$DER_FILE" 2>/dev/null

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ DER certificate generated: $DER_FILE${NC}"
else
    echo -e "${YELLOW}⚠ DER generation failed (not critical)${NC}"
fi

echo ""
echo -e "${GREEN}=== Certificate Generation Complete ===${NC}"
echo ""

# Verify the certificate
echo "Certificate Details:"
echo "-------------------"
openssl x509 -in "$CERT_FILE" -noout -subject -issuer -dates

echo ""
echo "Verification:"
echo "-------------"
# Verify certificate matches key
CERT_MODULUS=$(openssl x509 -noout -modulus -in "$CERT_FILE" | openssl md5)
KEY_MODULUS=$(openssl rsa -noout -modulus -in "$KEY_FILE" 2>/dev/null | openssl md5)

if [ "$CERT_MODULUS" == "$KEY_MODULUS" ]; then
    echo -e "${GREEN}✓ Certificate and private key match${NC}"
else
    echo -e "${RED}✗ Certificate and private key DO NOT match!${NC}"
    exit 1
fi

# File sizes
echo ""
echo "Generated Files:"
echo "----------------"
ls -lh "$CERT_FILE" "$KEY_FILE" "$DER_FILE" 2>/dev/null | awk '{print $9, "("$5")"}'

echo ""
echo -e "${YELLOW}⚠ IMPORTANT SECURITY NOTES:${NC}"
echo ""
echo "1. These are SELF-SIGNED certificates for development/testing"
echo "2. Browsers will show security warnings (expected)"
echo "3. For PRODUCTION, use CA-signed certificates"
echo "4. Keep $KEY_FILE SECURE - it contains your private key"
echo "5. Add *.pem and *.key to .gitignore to avoid committing secrets"
echo ""
echo "Next Steps:"
echo "-----------"
echo "1. Enable HTTPS in menuconfig:"
echo "   idf.py menuconfig → Component config → TinyBMS Gateway → Enable HTTPS"
echo ""
echo "2. Build and flash:"
echo "   idf.py build flash"
echo ""
echo "3. Access gateway at:"
echo "   https://$HOSTNAME (or https://[gateway-ip])"
echo ""
echo "4. Accept security warning in browser (development only)"
echo ""
echo -e "${GREEN}Done!${NC}"
