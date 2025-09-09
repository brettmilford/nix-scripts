#!/bin/bash

set -e

if [ $# -ne 1 ]; then
    echo "Usage: $(basename "$0") <URL to test>"
    exit 1
fi

TEST_URL="$1"
TEST_EMAIL="nonexistent@example.com"
TEST_USERNAME="baduser"
TEST_PASSWORD="wrongpassword"

echo "Testing Immich API authentication failures..."
for i in {1..5}; do
    echo "Attempt $i: Testing failed login..."
    curl -X POST \
        -H "Content-Type: application/json" \
        -d "{\"email\":\"$TEST_EMAIL\",\"password\":\"$TEST_PASSWORD\"}" \
        "$TEST_URL/api/auth/login" \
        -v
    echo ""
    sleep 2
done

echo "Testing Paperless API authentication failures..."
for i in {1..5}; do
    echo "Attempt $i: Bad API login..."

    response=$(curl -s -w "\nHTTP_CODE:%{http_code}" \
        -X POST \
        -H "Content-Type: application/json" \
        -d "{\"username\":\"$TEST_USERNAME\",\"password\":\"$TEST_PASSWORD\"}" \
        "$TEST_URL/api/token/")

    http_code=$(echo "$response" | tail -n1 | cut -d: -f2)
    response_body=$(echo "$response" | head -n -1)

    echo "  HTTP Code: $http_code"
    if [[ "$response_body" == *"detail"* ]]; then
        echo "  Response: $(echo "$response_body" | jq -r '.detail' 2>/dev/null || echo "$response_body")"
    fi

    sleep 2
done


echo "Testing Nextcloud WebDAV authentication failures..."
for i in {1..5}; do
    echo "Attempt $i: Testing WebDAV with bad credentials..."
    response=$(curl -s -w "\nHTTP_CODE:%{http_code}" \
        -u "${TEST_USERNAME}:${TEST_PASSWORD}" \
        -X PROPFIND \
        -H "Depth: 0" \
        "${TEST_URL}/remote.php/dav/")

    http_code=$(echo "$response" | tail -n1 | cut -d: -f2)
    response_body=$(echo "$response" | head -n -1)

    echo "  HTTP Code: $http_code"
    if [[ "$response_body" == *"Sabre"* ]] || [[ "$response_body" == *"exception"* ]]; then
        echo "  Response: Authentication failed"
    elif [[ "$response_body" != "" ]]; then
        echo "  Response: $response_body"
    fi
    sleep 2
done

echo ""
echo "Testing Nextcloud OCS API authentication failures..."
for i in {1..5}; do
    echo "Attempt $i: Testing OCS API with bad credentials..."
    response=$(curl -s -w "\nHTTP_CODE:%{http_code}" \
        -u "${TEST_USERNAME}:${TEST_PASSWORD}" \
        -H "OCS-APIRequest: true" \
        -X GET \
        "${TEST_URL}/ocs/v2.php/cloud/users")

    http_code=$(echo "$response" | tail -n1 | cut -d: -f2)
    response_body=$(echo "$response" | head -n -1)

    echo "  HTTP Code: $http_code"
    if [[ "$response_body" == *"Current user is not logged in"* ]]; then
        echo "  Response: Authentication failed"
    elif [[ "$response_body" != "" ]]; then
        echo "  Response: $response_body"
    fi
    sleep 2
done

echo ""
echo "Testing Nextcloud file access authentication failures..."
for i in {1..5}; do
    echo "Attempt $i: Testing file access with bad credentials..."
    response=$(curl -s -w "\nHTTP_CODE:%{http_code}" \
        -u "${TEST_USERNAME}:${TEST_PASSWORD}" \
        -X PROPFIND \
        -H "Depth: 1" \
        "${TEST_URL}/remote.php/dav/files/${TEST_USERNAME}/")

    http_code=$(echo "$response" | tail -n1 | cut -d: -f2)
    response_body=$(echo "$response" | head -n -1)

    echo "  HTTP Code: $http_code"
    if [[ "$http_code" == "401" ]]; then
        echo "  Response: Unauthorized access denied"
    elif [[ "$response_body" != "" ]]; then
        echo "  Response: $response_body"
    fi
    sleep 2
done

echo "Testing Keycloak token endpoint authentication failures..."
for i in {1..5}; do
    echo "Attempt $i: Testing token endpoint with bad credentials..."
    response=$(curl -s -w "\nHTTP_CODE:%{http_code}" \
        -X POST \
        -H "Content-Type: application/x-www-form-urlencoded" \
        -d "grant_type=password" \
        -d "client_id=admin-cli" \
        -d "username=$TEST_USERNAME" \
        -d "password=$TEST_PASSWORD" \
        "$TEST_URL/realms/master/protocol/openid-connect/token")

    http_code=$(echo "$response" | tail -n1 | cut -d: -f2)
    response_body=$(echo "$response" | head -n -1)

    echo "  HTTP Code: $http_code"
    if [[ "$response_body" == *"error"* ]]; then
        echo "  Response: $(echo "$response_body" | jq -r '.error_description // .error' 2>/dev/null || echo "$response_body")"
    elif [[ "$response_body" != "" ]]; then
        echo "  Response: $response_body"
    fi
    sleep 2
done

echo ""
echo "Testing Keycloak admin console authentication failures..."
for i in {1..5}; do
    echo "Attempt $i: Testing admin login with bad credentials..."
    response=$(curl -s -w "\nHTTP_CODE:%{http_code}" \
        -X POST \
        -H "Content-Type: application/x-www-form-urlencoded" \
        -d "username=$TEST_USERNAME" \
        -d "password=$TEST_PASSWORD" \
        -d "credentialId=" \
        "$TEST_URL/realms/master/login-actions/authenticate")

    http_code=$(echo "$response" | tail -n1 | cut -d: -f2)
    response_body=$(echo "$response" | head -n -1)

    echo "  HTTP Code: $http_code"
    if [[ "$response_body" == *"error"* ]] || [[ "$response_body" == *"invalid"* ]]; then
        echo "  Response: Authentication failed"
    elif [[ "$response_body" != "" ]]; then
        echo "  Response: Login attempt processed"
    fi
    sleep 2
done

echo ""
echo "Testing Keycloak account console authentication failures..."
for i in {1..5}; do
    echo "Attempt $i: Testing account console with bad credentials..."
    response=$(curl -s -w "\nHTTP_CODE:%{http_code}" \
        -X POST \
        -H "Content-Type: application/x-www-form-urlencoded" \
        -d "username=$TEST_USERNAME" \
        -d "password=$TEST_PASSWORD" \
        "$TEST_URL/realms/master/account/login-actions/authenticate")

    http_code=$(echo "$response" | tail -n1 | cut -d: -f2)
    echo "  HTTP Code: $http_code"
    if [[ "$http_code" == "401" ]] || [[ "$http_code" == "403" ]]; then
        echo "  Response: Unauthorized access denied"
    fi
    sleep 2
done
