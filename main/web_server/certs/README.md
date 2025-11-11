# HTTPS Certificates for TinyBMS Gateway

This directory contains HTTPS/TLS certificates for the web server.

## ⚠️ SECURITY WARNING

**DO NOT use the default certificates in production!**

The default certificates are self-signed and provided for development/testing only.
For production deployment, you MUST generate your own certificates.

## Quick Start - Generate Certificates

### Option 1: Using the provided script (Recommended)

```bash
cd main/web_server/certs
./generate_certs.sh
```

This will generate:
- `server_cert.pem` - Self-signed certificate (valid 10 years)
- `server_key.pem` - Private key (2048-bit RSA)
- `server_cert.der` - Certificate in DER format (optional)

### Option 2: Manual generation with OpenSSL

#### Step 1: Generate private key (2048-bit RSA)

```bash
openssl genrsa -out server_key.pem 2048
```

#### Step 2: Generate self-signed certificate (valid 10 years)

```bash
openssl req -new -x509 -key server_key.pem -out server_cert.pem -days 3650 \
  -subj "/C=XX/ST=State/L=City/O=TinyBMS/OU=Gateway/CN=tinybms-gw.local"
```

Customize the subject fields:
- `/C=XX` - Country code (2 letters)
- `/ST=State` - State or province
- `/L=City` - City
- `/O=TinyBMS` - Organization name
- `/OU=Gateway` - Organizational unit
- `/CN=tinybms-gw.local` - Common Name (hostname or IP)

**Important**: The CN (Common Name) should match your gateway's hostname or IP address.

#### Step 3: Verify certificates

```bash
# View certificate details
openssl x509 -in server_cert.pem -text -noout

# Verify certificate matches private key
openssl x509 -noout -modulus -in server_cert.pem | openssl md5
openssl rsa -noout -modulus -in server_key.pem | openssl md5
# The MD5 hashes should match
```

## Production-Grade Certificates

For production, consider using certificates from a Certificate Authority (CA):

### Option A: Let's Encrypt (Free, but requires public domain)

Not directly applicable for embedded devices, but can be used if your gateway is publicly accessible.

### Option B: Private CA

If you manage multiple gateways, create your own Certificate Authority:

```bash
# 1. Create CA private key
openssl genrsa -out ca_key.pem 4096

# 2. Create CA certificate (valid 20 years)
openssl req -new -x509 -key ca_key.pem -out ca_cert.pem -days 7300 \
  -subj "/C=XX/ST=State/L=City/O=TinyBMS/OU=CA/CN=TinyBMS Root CA"

# 3. Create server certificate signing request (CSR)
openssl req -new -key server_key.pem -out server.csr \
  -subj "/C=XX/ST=State/L=City/O=TinyBMS/OU=Gateway/CN=tinybms-gw.local"

# 4. Sign server certificate with CA (valid 5 years)
openssl x509 -req -in server.csr -CA ca_cert.pem -CAkey ca_key.pem \
  -CAcreateserial -out server_cert.pem -days 1825
```

Then distribute `ca_cert.pem` to all clients that need to trust your gateways.

## Embedding Certificates in Firmware

The certificates are automatically embedded during build if present in this directory.

The build system (CMakeLists.txt) includes these lines:

```cmake
# Embed certificates if HTTPS is enabled
if(CONFIG_TINYBMS_WEB_HTTPS_ENABLED)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/web_server/certs/server_cert.pem")
        target_add_binary_data(${COMPONENT_TARGET}
            "web_server/certs/server_cert.pem" TEXT)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/web_server/certs/server_key.pem")
        target_add_binary_data(${COMPONENT_TARGET}
            "web_server/certs/server_key.pem" TEXT)
    endif()
endif()
```

### File Requirements

- **server_cert.pem**: Certificate in PEM format (text)
- **server_key.pem**: Private key in PEM format (text, unencrypted)

**Note**: Private keys should NOT be password-protected when embedding in firmware.

## Alternative: Load Certificates from NVS/SPIFFS

Instead of embedding certificates in firmware, you can load them at runtime:

### Advantages:
- Update certificates without reflashing firmware
- Different certificates per device
- Easier certificate rotation

### Implementation (Future Enhancement):

```c
// Load certificate from SPIFFS
FILE *f = fopen("/spiffs/server_cert.pem", "r");
// Read into buffer
// Configure HTTP server with runtime certificate
```

## Security Best Practices

### Certificate Management

1. **Key Security**:
   - Keep private keys secure
   - Never commit private keys to version control
   - Use `.gitignore` to exclude `.pem` and `.key` files

2. **Certificate Rotation**:
   - Rotate certificates before expiration
   - Recommended: 1-2 year validity for production
   - Set calendar reminders for rotation

3. **Multiple Environments**:
   - Use different certificates for dev/test/prod
   - Document which certificates are deployed where

### .gitignore Recommendations

Add to your `.gitignore`:

```gitignore
# Exclude actual certificate files
*.pem
*.key
*.csr
*.der
*.p12
*.pfx

# Keep only README and generation scripts
!certs/README.md
!certs/generate_certs.sh
```

## Troubleshooting

### Problem: "Certificate verification failed" in browser

**Solution**:
- For self-signed certificates, you'll see a security warning in browsers
- This is expected behavior
- Users can proceed by accepting the risk (development only)
- For production, use CA-signed certificates

### Problem: "Certificate expired"

**Solution**: Regenerate certificates with longer validity or rotate regularly

### Problem: "Hostname doesn't match certificate"

**Solution**:
- Ensure CN in certificate matches your gateway's hostname/IP
- Consider using Subject Alternative Names (SAN) for multiple hostnames

### Problem: Embedding fails during build

**Solution**:
- Verify files exist in `main/web_server/certs/`
- Check file names match exactly: `server_cert.pem` and `server_key.pem`
- Ensure `CONFIG_TINYBMS_WEB_HTTPS_ENABLED=y` in sdkconfig

## Additional Resources

- [ESP-IDF HTTPS Server Example](https://github.com/espressif/esp-idf/tree/master/examples/protocols/https_server)
- [OpenSSL Documentation](https://www.openssl.org/docs/)
- [Mozilla SSL Configuration Generator](https://ssl-config.mozilla.org/)

## Support

For issues related to HTTPS configuration, please refer to:
- Main documentation: `archive/docs/ANALYSE_COMPLETE_CODE_2025.md`
- Security analysis: See section on HTTPS implementation
