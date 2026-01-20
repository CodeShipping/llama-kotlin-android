#!/bin/bash
set -e

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== LLaMA Kotlin Android Publisher ===${NC}"
echo ""

# Step 1: Get GPG passphrase
echo -n "Enter GPG passphrase: "
read -s GPG_PASS
echo ""

# Step 2: Build release AAR
echo -e "${YELLOW}Building release AAR...${NC}"
./gradlew clean :app:assembleRelease

if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Release AAR built${NC}"

# Step 3: Publish to local repo with signing
echo -e "${YELLOW}Signing and publishing artifacts...${NC}"
./gradlew :app:publishReleasePublicationToLocalRepository -Psigning.password="$GPG_PASS"

if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Build successful${NC}"

# Step 4: Create bundle zip
echo -e "${YELLOW}Creating bundle.zip...${NC}"
cd app/build/repo
rm -f ../bundle.zip
zip -r ../bundle.zip org
cd ../../..

BUNDLE_PATH="$(pwd)/app/build/bundle.zip"
BUNDLE_SIZE=$(ls -lh "$BUNDLE_PATH" | awk '{print $5}')

echo -e "${GREEN}✓ Bundle created: $BUNDLE_PATH ($BUNDLE_SIZE)${NC}"

# Step 5: Upload to Maven Central
echo ""
echo -e "${YELLOW}Uploading to Maven Central...${NC}"

# Get credentials from gradle.properties
MAVEN_USER=$(grep "mavenCentralUsername" ~/.gradle/gradle.properties | cut -d'=' -f2)
MAVEN_PASS=$(grep "mavenCentralPassword" ~/.gradle/gradle.properties | cut -d'=' -f2)

if [ -z "$MAVEN_USER" ] || [ -z "$MAVEN_PASS" ]; then
    echo -e "${RED}Maven Central credentials not found in ~/.gradle/gradle.properties${NC}"
    echo -e "Please upload manually:"
    echo -e "  1. Go to https://central.sonatype.com/"
    echo -e "  2. Click Publish → Upload Bundle"
    echo -e "  3. Upload: $BUNDLE_PATH"
    exit 1
fi

# Upload using Sonatype Central API
RESPONSE=$(curl -s -w "\n%{http_code}" \
    -u "$MAVEN_USER:$MAVEN_PASS" \
    -X POST \
    -F "bundle=@$BUNDLE_PATH" \
    "https://central.sonatype.com/api/v1/publisher/upload")

HTTP_CODE=$(echo "$RESPONSE" | tail -1)
BODY=$(echo "$RESPONSE" | sed '$d')

if [ "$HTTP_CODE" = "201" ] || [ "$HTTP_CODE" = "200" ]; then
    echo -e "${GREEN}✓ Upload successful!${NC}"
    echo -e "${GREEN}Deployment ID: $BODY${NC}"
    echo ""
    echo -e "Next steps:"
    echo -e "  1. Go to https://central.sonatype.com/publishing/deployments"
    echo -e "  2. Find your deployment and click 'Publish'"
    echo -e "  3. Wait for validation and sync to Maven Central"
else
    echo -e "${RED}Upload failed (HTTP $HTTP_CODE)${NC}"
    echo "$BODY"
    echo ""
    echo -e "Please upload manually:"
    echo -e "  1. Go to https://central.sonatype.com/"
    echo -e "  2. Click Publish → Upload Bundle"  
    echo -e "  3. Upload: $BUNDLE_PATH"
fi
