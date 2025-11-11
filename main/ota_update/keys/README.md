# OTA Signature Keys

This directory contains cryptographic keys for OTA firmware signature verification.

## ⚠️ CRITICAL SECURITY INFORMATION

**PRIVATE KEYS MUST NEVER BE COMMITTED TO VERSION CONTROL**

- Private keys give ability to sign malicious firmware
- Once compromised, attacker can deploy arbitrary code to all gateways
- Keep private keys in secure, encrypted storage
- Use hardware security modules (HSM) for production key storage

## Key Generation (One Time Setup)

### Generate RSA Key Pair (2048-bit)

```bash
cd main/ota_update/keys

# Generate private key (KEEP SECRET!)
openssl genrsa -out ota_private_key.pem 2048

# Extract public key (embed in firmware)
openssl rsa -in ota_private_key.pem -pubout -out ota_public_key.pem

# Set secure permissions
chmod 600 ota_private_key.pem
chmod 644 ota_public_key.pem
```

### Generate RSA Key Pair (4096-bit - More Secure)

```bash
# For higher security (slower, but recommended for production)
openssl genrsa -out ota_private_key.pem 4096
openssl rsa -in ota_private_key.pem -pubout -out ota_public_key.pem
```

### Verify Keys

```bash
# Check private key
openssl rsa -in ota_private_key.pem -check

# Check public key
openssl rsa -in ota_public_key.pem -pubin -text -noout

# Verify keys match
openssl rsa -in ota_private_key.pem -pubout | diff -s - ota_public_key.pem
```

## Key Usage

### Public Key (ota_public_key.pem)

- **Purpose**: Verify firmware signatures
- **Location**: Embedded in gateway firmware
- **Security**: Can be public (but integrity must be protected)
- **Action**: Place in this directory before building firmware

### Private Key (ota_private_key.pem)

- **Purpose**: Sign firmware updates
- **Location**: Secure build server ONLY
- **Security**: MUST BE KEPT SECRET
- **Action**: NEVER commit to git, NEVER upload to gateway

## Signing Firmware

### Manual Signing

```bash
# Sign firmware binary
openssl dgst -sha256 -sign ota_private_key.pem \
  -out firmware.sig build/tinybms-gw.bin

# Upload both files to gateway:
# - build/tinybms-gw.bin (firmware)
# - firmware.sig (signature)
```

### Using Helper Script

```bash
# Use the provided script
../../scripts/sign_firmware.sh build/tinybms-gw.bin ota_private_key.pem

# Output: build/tinybms-gw.bin.sig
```

### Verify Signature Locally (Before Upload)

```bash
# Extract hash from firmware
openssl dgst -sha256 -binary build/tinybms-gw.bin > firmware.hash

# Verify signature
openssl rsautl -verify -inkey ota_public_key.pem -pubin \
  -in firmware.sig -out verified.hash

# Compare hashes
diff firmware.hash verified.hash && echo "✓ Signature valid"
```

## Embedding Public Key in Firmware

The build system automatically embeds `ota_public_key.pem` if present in this directory.

### CMakeLists.txt Configuration

```cmake
# In main/CMakeLists.txt
if(CONFIG_TINYBMS_OTA_SIGNATURE_VERIFY_ENABLED)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ota_update/keys/ota_public_key.pem")
        target_add_binary_data(${COMPONENT_TARGET}
            "ota_update/keys/ota_public_key.pem" TEXT)
        message(STATUS "OTA public key embedded from ota_update/keys/")
    else()
        message(WARNING "OTA signature verification enabled but no public key found!")
    endif()
endif()
```

### Verify Embedding

After building:

```bash
# Check if key is embedded
strings build/tinybms-gw.bin | grep "BEGIN PUBLIC KEY"

# Should output:
# -----BEGIN PUBLIC KEY-----
```

## Key Rotation

If private key is compromised or for routine security maintenance:

### 1. Generate New Key Pair

```bash
# Backup old keys
mv ota_private_key.pem ota_private_key.pem.old
mv ota_public_key.pem ota_public_key.pem.old

# Generate new keys
openssl genrsa -out ota_private_key.pem 2048
openssl rsa -in ota_private_key.pem -pubout -out ota_public_key.pem
```

### 2. Rebuild Firmware with New Public Key

```bash
# Clean and rebuild to embed new public key
idf.py clean build
```

### 3. Deploy New Firmware

**Important**: You must use the OLD private key to sign the firmware update that contains the NEW public key.

```bash
# Sign new firmware with OLD private key
openssl dgst -sha256 -sign ota_private_key.pem.old \
  -out firmware.sig build/tinybms-gw.bin

# Upload and verify with old key
# After update, device now trusts NEW key
```

### 4. Use New Key for Future Updates

All subsequent firmware updates must be signed with the NEW private key.

## Security Best Practices

### Key Storage

1. **Private Key**:
   - Store in password-protected, encrypted volume
   - Use HSM (Hardware Security Module) for production
   - Limit access to authorized build servers only
   - Enable audit logging for all key usage

2. **Public Key**:
   - Can be public, but protect integrity
   - Verify checksum before embedding
   - Consider code signing the entire firmware image

### Access Control

```bash
# Recommended permissions
chmod 600 ota_private_key.pem  # Owner read/write only
chmod 644 ota_public_key.pem   # Everyone read, owner write

# Recommended ownership
chown root:root ota_private_key.pem
```

### Audit Trail

Maintain log of all firmware signatures:

```bash
# Log each signature
echo "$(date): Signed $(sha256sum build/tinybms-gw.bin)" >> signature_log.txt
```

### Key Backup

```bash
# Encrypted backup of private key
gpg --symmetric --cipher-algo AES256 ota_private_key.pem

# Store ota_private_key.pem.gpg in secure, offsite location
# NEVER store unencrypted backup
```

## Troubleshooting

### Problem: "Failed to parse public key"

**Cause**: Invalid or corrupted key file

**Solution**:
```bash
# Verify key format
openssl rsa -in ota_public_key.pem -pubin -text -noout

# Should start with: -----BEGIN PUBLIC KEY-----
head -1 ota_public_key.pem
```

### Problem: "Signature verification failed"

**Possible causes**:
1. Firmware modified after signing
2. Wrong private/public key pair
3. Signature file corrupted

**Solution**:
```bash
# Verify keys match
openssl rsa -in ota_private_key.pem -pubout > temp_public.pem
diff temp_public.pem ota_public_key.pem

# Re-sign firmware
openssl dgst -sha256 -sign ota_private_key.pem \
  -out firmware.sig build/tinybms-gw.bin
```

### Problem: "No public key embedded"

**Cause**: Key file not present during build

**Solution**:
```bash
# Verify file exists
ls -la main/ota_update/keys/ota_public_key.pem

# Clean and rebuild
idf.py clean build

# Check build log for "OTA public key embedded"
```

## .gitignore Configuration

**CRITICAL**: Add to `.gitignore`:

```gitignore
# NEVER commit private keys!
**/ota_private_key.pem
**/ota_private_key*.pem
**/*.key
**/*.pem.gpg

# Keep only public keys and documentation
!ota_public_key.pem
!**/README.md
```

## Emergency Key Revocation

If private key is compromised:

### Immediate Actions

1. **Stop all OTA updates** immediately
2. **Generate new key pair** (see above)
3. **Rebuild and deploy** firmware with new public key
   - Sign with OLD key (last time)
   - Emergency deployment to all gateways
4. **Invalidate compromised key** in key management system
5. **Audit all firmware** signed with compromised key
6. **Incident report** to security team

### Communication

Notify all stakeholders:
- Development team
- Operations team
- Security team
- Customers (if applicable)

## Additional Resources

- [ESP-IDF Secure Boot](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/secure-boot-v2.html)
- [NIST Cryptographic Standards](https://csrc.nist.gov/projects/cryptographic-standards-and-guidelines)
- [OpenSSL Documentation](https://www.openssl.org/docs/)

## Support

For security-related questions or to report key compromise:
- Internal: Contact security team immediately
- External: See SECURITY.md in repository root
