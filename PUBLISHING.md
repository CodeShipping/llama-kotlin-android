# Publishing to Maven Central

This guide explains how to publish `llama-kotlin-android` to Maven Central.

## Prerequisites

### 1. Sonatype Central Account

1. Go to [central.sonatype.com](https://central.sonatype.com/)
2. Create an account (or sign in with GitHub)
3. Register your namespace (e.g., `org.codeshipping`)
4. Verify domain ownership via DNS TXT record

### 2. GPG Key

Generate a GPG key for signing artifacts:

```bash
# Generate key
gpg --full-generate-key
# Choose: RSA and RSA, 4096 bits, no expiration

# List keys to get key ID
gpg --list-secret-keys --keyid-format SHORT
# Example output shows key ID like: AF5FBB39

# Export public key (for your records)
gpg --armor --export YOUR_KEY_ID > public-key.asc

# Export private key (keep secure!)
gpg --armor --export-secret-keys YOUR_KEY_ID > private-key.asc
```

### 3. Upload Public Key to Keyservers

Sonatype needs to verify signatures using your public key:

```bash
# Method 1: Via HTTP POST (recommended if HKP is blocked)
gpg --armor --export YOUR_KEY_ID | curl -X POST --data-urlencode "keytext@-" "https://keyserver.ubuntu.com/pks/add"

# Method 2: Via GPG command
gpg --keyserver keyserver.ubuntu.com --send-keys YOUR_KEY_ID
```

Verify upload:
```bash
curl "https://keyserver.ubuntu.com/pks/lookup?op=get&search=0xYOUR_KEY_ID" | head -5
```

## Configuration

### ~/.gradle/gradle.properties

Create or edit `~/.gradle/gradle.properties`:

```properties
# Maven Central Credentials (from central.sonatype.com → View Account → Generate User Token)
mavenCentralUsername=YOUR_TOKEN_USERNAME
mavenCentralPassword=YOUR_TOKEN_PASSWORD

# GPG Signing (key ID only - password prompted at runtime)
signing.keyId=YOUR_8_CHAR_KEY_ID
```

### GPG Private Key File

Store your private key (ASCII armored) at:
```
~/personal-workspace/Importants/gpg-maven-signing-key.asc
```

**Keep this file secure and never commit it to version control!**

## Publishing

### Quick Publish

Run the publish script:

```bash
./publish.sh
```

This will:
1. Prompt for your GPG passphrase (hidden input)
2. Build release AAR
3. Sign all artifacts
4. Create bundle.zip
5. Upload to Maven Central

### Manual Steps

If automatic upload fails:

1. Build and sign:
   ```bash
   ./gradlew clean :app:assembleRelease
   ./gradlew :app:publishReleasePublicationToLocalRepository -Psigning.password="YOUR_PASSPHRASE"
   ```

2. Create bundle:
   ```bash
   cd app/build/repo && zip -r ../bundle.zip org
   ```

3. Upload manually:
   - Go to [central.sonatype.com](https://central.sonatype.com/)
   - Click **Publish** → **Upload Bundle**
   - Upload `app/build/bundle.zip`

4. Verify and publish:
   - Go to [Deployments](https://central.sonatype.com/publishing/deployments)
   - Review validation results
   - Click **Publish** when ready

## Artifact Information

| Field | Value |
|-------|-------|
| **Group ID** | `org.codeshipping` |
| **Artifact ID** | `llama-kotlin-android` |
| **Version** | `0.1.0` |
| **Packaging** | `aar` |

## Usage After Publishing

Users can add the dependency:

```kotlin
// build.gradle.kts
dependencies {
    implementation("org.codeshipping:llama-kotlin-android:0.1.0")
}
```

## Troubleshooting

### "Invalid signature - Could not find public key"

Your GPG public key isn't on keyservers yet. Upload it:

```bash
gpg --armor --export YOUR_KEY_ID | curl -X POST --data-urlencode "keytext@-" "https://keyserver.ubuntu.com/pks/add"
```

Wait 5-10 minutes for propagation, then retry.

### "Cannot perform signing task - no configured signatory"

1. Ensure `signing.keyId` is set in `~/.gradle/gradle.properties`
2. Ensure GPG private key file exists at the configured path
3. Ensure you're passing the password: `-Psigning.password="..."`

### Build Errors with Resources

This library module shouldn't have themes, icons, or string resources. The `res/` folder should be empty or removed entirely.

## Security Notes

- **Never commit** credentials or private keys to version control
- Store credentials in `~/.gradle/gradle.properties` (outside project)
- Store GPG private key in a secure location outside the project
- GPG passphrase is prompted at runtime for security

## Links

- [Maven Central Portal](https://central.sonatype.com/)
- [Publishing Guide](https://central.sonatype.org/publish/)
- [Keyserver Status](https://keyserver.ubuntu.com/)
