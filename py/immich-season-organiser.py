"""
Immich Seasonal Album Organizer
Organizes photos into seasonal albums using Australian seasons
"""

import os
import sys
import requests
from datetime import datetime
from typing import Dict, List, Optional

# Configuration constants
try:
    IMMICH_SERVER_URL = sys.argv[1]
except IndexError:
    print("Usage: immich-download-trashed <IMMICH_SERVER_URL>")
    print("Environment variables required:")
    print("  IMMICH_API_KEY - Your Immich API key")
    sys.exit(1)

API_KEY = os.environ.get("IMMICH_API_KEY")
if not API_KEY:
    print("Error: IMMICH_API_KEY environment variable is required")
    sys.exit(1)

PAGE_SIZE = 1000
MAX_RETRIES = 3

# Australian seasons mapping
AUSTRALIAN_SEASONS = {
    12: "Summer",
    1: "Summer",
    2: "Summer",
    3: "Autumn",
    4: "Autumn",
    5: "Autumn",
    6: "Winter",
    7: "Winter",
    8: "Winter",
    9: "Spring",
    10: "Spring",
    11: "Spring",
}


class ImmichSeasonalOrganizer:
    def __init__(self, server_url: str, api_key: str):
        self.server_url = server_url.rstrip("/")
        self.headers = {"X-API-Key": api_key, "Content-Type": "application/json"}
        self.session = requests.Session()
        self.session.headers.update(self.headers)

    def get_all_assets(self) -> List[Dict]:
        """Get all assets using paginated search"""
        all_assets = []
        next_page = None
        page_num = 1

        while True:
            print(f"Fetching assets page {page_num}...")

            url = f"{self.server_url}/api/search/metadata"
            data = {}

            # Use nextPage token if available, otherwise start from beginning
            if next_page:
                data["page"] = next_page

            try:
                response = self.session.post(url, json=data)
                response.raise_for_status()
                result = response.json()

                # Debug: Print response structure on first page
                if page_num == 1:
                    print(f"API response keys: {list(result.keys())}")
                    if "assets" in result:
                        print(f"Assets object keys: {list(result['assets'].keys())}")

                # Handle different possible response structures
                if "assets" in result and isinstance(result["assets"], dict):
                    # Structure: {"assets": {"items": [...], "nextPage": "2"}}
                    assets = result["assets"].get("items", [])
                    next_page = result["assets"].get("nextPage")
                    total = result["assets"].get("total", 0)
                elif "assets" in result and isinstance(result["assets"], list):
                    # Structure: {"assets": [...]}
                    assets = result["assets"]
                    next_page = result.get("nextPage")
                    total = len(assets)
                else:
                    # Structure: direct list or other format
                    assets = result if isinstance(result, list) else []
                    next_page = (
                        result.get("nextPage") if isinstance(result, dict) else None
                    )
                    total = len(assets)

                if not assets:
                    print(f"No assets found on page {page_num}")
                    break

                all_assets.extend(assets)
                print(f"Got {len(assets)} assets from page {page_num}")
                print(
                    f"Total assets so far: {len(all_assets)} (API reports total: {total})"
                )

                # Check if we have more pages
                if not next_page:
                    print("No more pages available")
                    break

                print(f"Next page token: {next_page}")
                page_num += 1

            except requests.exceptions.RequestException as e:
                print(f"Error fetching assets page {page_num}: {e}")
                break

        print(f"Total assets found: {len(all_assets)}")
        return all_assets

    def get_albums(self) -> List[Dict]:
        """Get all existing albums"""
        url = f"{self.server_url}/api/albums"

        try:
            response = self.session.get(url)
            response.raise_for_status()
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"Error fetching albums: {e}")
            return []

    def create_album(self, album_name: str) -> Optional[str]:
        """Create a new album and return its ID"""
        url = f"{self.server_url}/api/albums"

        data = {"albumName": album_name, "description": f"Photos from {album_name}"}

        try:
            response = self.session.post(url, json=data)
            response.raise_for_status()
            album_data = response.json()
            album_id = album_data.get("id")
            print(f"Created album: {album_name}")
            return album_id
        except requests.exceptions.RequestException as e:
            print(f"Error creating album '{album_name}': {e}")
            return None

    def add_assets_to_album(self, album_id: str, asset_ids: List[str]) -> bool:
        """Add assets to an album"""
        if not asset_ids:
            return True

        url = f"{self.server_url}/api/albums/{album_id}/assets"
        data = {"ids": asset_ids}

        try:
            response = self.session.put(url, json=data)
            response.raise_for_status()
            print(f"Added {len(asset_ids)} assets to album")
            return True
        except requests.exceptions.RequestException as e:
            print(f"Error adding assets to album: {e}")
            return False


def get_season_from_date(date_str: str) -> tuple:
    """Get Australian season and year from date string"""
    if not date_str:
        return None, None

    try:
        # Try different date formats
        date_formats = [
            "%Y-%m-%dT%H:%M:%S.%fZ",
            "%Y-%m-%dT%H:%M:%SZ",
            "%Y-%m-%d %H:%M:%S",
            "%Y-%m-%d",
        ]

        date_obj = None
        for fmt in date_formats:
            try:
                date_obj = datetime.strptime(date_str, fmt)
                break
            except ValueError:
                continue

        if not date_obj:
            return None, None

        month = date_obj.month
        year = date_obj.year

        # For Australian seasons, Janurary and Feburary belong to the previous year.
        if month == 1 or month == 2:
            year -= 1

        season = AUSTRALIAN_SEASONS.get(month)
        return season, year

    except Exception as e:
        print(f"Error parsing date '{date_str}': {e}")
        return None, None


def main():
    """Main function to organize photos into seasonal albums"""

    # Initialize organizer
    organizer = ImmichSeasonalOrganizer(IMMICH_SERVER_URL, API_KEY)

    print("Fetching all assets...")
    assets = organizer.get_all_assets()

    if not assets:
        print("No assets found")
        return

    # Group assets by season and year
    print("Grouping assets by season...")
    seasonal_groups = {}
    skipped_count = 0
    no_date_assets = []

    for asset in assets:
        # Skip archived or trashed assets
        if asset.get("isArchived") or asset.get("isTrashed"):
            skipped_count += 1
            continue

        # Get the date taken (try different field names)
        date_taken = (
            asset.get("fileCreatedAt")
            or asset.get("exifInfo", {}).get("dateTimeOriginal")
            or asset.get("localDateTime")
            or asset.get("createdAt")
        )

        if not date_taken:
            skipped_count += 1
            no_date_assets.append(
                {
                    "id": asset.get("id"),
                    "filename": asset.get("originalFileName", "Unknown"),
                    "path": asset.get("originalPath", "Unknown"),
                    "type": asset.get("type", "Unknown"),
                    "deviceId": asset.get("deviceId", "Unknown"),
                }
            )
            continue

        season, year = get_season_from_date(date_taken)

        # Seasonal albums beyond this are excessive and dates become speculative.
        if year < 2012:
            skipped_count += 1
            continue

        if season and year:
            album_name = f"{season} {year}"

            if album_name not in seasonal_groups:
                seasonal_groups[album_name] = []

            seasonal_groups[album_name].append(asset["id"])
        else:
            skipped_count += 1

    print(
        f"Organized {len(assets) - skipped_count} photos into {len(seasonal_groups)} seasonal groups"
    )
    print(f"Skipped {skipped_count} assets (archived, trashed, or no date)")

    # Get existing albums
    print("Fetching existing albums...")
    existing_albums = organizer.get_albums()
    album_lookup = {album["albumName"]: album["id"] for album in existing_albums}

    # Create albums and add photos
    for album_name, asset_ids in seasonal_groups.items():
        print(f"\nProcessing album: {album_name} ({len(asset_ids)} photos)")

        # Get or create album
        if album_name in album_lookup:
            album_id = album_lookup[album_name]
            print(f"Album '{album_name}' already exists")
        else:
            album_id = organizer.create_album(album_name)

        if album_id:
            # Add assets to album in batches
            batch_size = 100
            for i in range(0, len(asset_ids), batch_size):
                batch = asset_ids[i: i + batch_size]
                batch_num = i // batch_size + 1
                total_batches = (len(asset_ids) + batch_size - 1) // batch_size

                print(f"Adding batch {batch_num}/{total_batches} ({len(batch)} assets)")
                success = organizer.add_assets_to_album(album_id, batch)
                if not success:
                    print(f"Failed to add batch {batch_num} to {album_name}")
        else:
            print(f"Failed to create album: {album_name}")

    # Report assets without dates for investigation
    if no_date_assets:
        print(
            f"\nAssets skipped due to missing date information ({len(no_date_assets)} total):"
        )
        print("=" * 80)
        for asset in no_date_assets:
            print(f"ID: {asset['id']}")
            print(f"  Filename: {asset['filename']}")
            print(f"  Path: {asset['path']}")
            print(f"  Type: {asset['type']}")
            print(f"  Device: {asset['deviceId']}")
            print("-" * 40)

        print(
            "\nTo investigate these assets, you can view them in Immich using their IDs"
        )
        print(f"Example: {IMMICH_SERVER_URL}/photos/{no_date_assets[0]['id']}")

    print("\nSeasonal organization complete!")


if __name__ == "__main__":
    main()
