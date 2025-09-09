"""
Immich Trash Downloader
Downloads all items currently in the trash
"""

import requests
from pathlib import Path
from datetime import datetime
from typing import Dict, List
import sys
import time

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

DOWNLOAD_DIR = "./trashed_items"
MAX_RETRIES = 3
RETRY_DELAY = 1  # seconds


class ImmichTrashDownloader:
    def __init__(self, server_url: str, api_key: str, download_dir: str):
        self.server_url = server_url.rstrip("/")
        self.download_dir = Path(download_dir)
        self.headers = {
            "X-API-Key": api_key,
            "Content-Type": "application/json",
        }
        self.session = requests.Session()
        self.session.headers.update(self.headers)

        # Create download directory
        self.download_dir.mkdir(parents=True, exist_ok=True)
        print(f"Download directory: {self.download_dir.absolute()}")

    def get_all_trashed_assets(self) -> List[Dict]:
        """Get all trashed assets using paginated search with trashedBefore parameter"""
        all_assets = []
        next_page = None
        page_num = 1

        # Get today's date in ISO format
        today = datetime.now().isoformat() + "Z"

        print(f"Searching for trashed assets using trashedBefore: {today}")

        while True:
            print(f"Fetching trashed assets page {page_num}...")

            url = f"{self.server_url}/api/search/metadata"
            data = {
                "trashedBefore": today,
                "withArchived": True,  # Include archived items that might also be trashed
            }

            # Use nextPage token if available
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
                        print(
                            f"Assets object keys: {list(result['assets'].keys())}"
                        )

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
                        result.get("nextPage")
                        if isinstance(result, dict)
                        else None
                    )
                    total = len(assets)

                if not assets:
                    print(f"No trashed assets found on page {page_num}")
                    break

                all_assets.extend(assets)
                print(f"Got {len(assets)} trashed assets from page {page_num}")
                print(
                    f"Total trashed assets so far: {len(all_assets)} (API reports total: {total})"
                )

                # Check if we have more pages
                if not next_page:
                    print("No more pages available")
                    break

                print(f"Next page token: {next_page}")
                page_num += 1

            except requests.exceptions.RequestException as e:
                print(f"Error fetching trashed assets page {page_num}: {e}")
                break

        print(f"Total trashed assets found: {len(all_assets)}")
        return all_assets

    def download_asset(self, asset: Dict) -> bool:
        """Download a single asset"""
        asset_id = asset.get("id")
        original_filename = asset.get("originalFileName", f"asset_{asset_id}")

        if not asset_id:
            print(f"No asset ID found for {original_filename}")
            return False

        # Create safe filename
        safe_filename = self.make_safe_filename(original_filename)
        filepath = self.download_dir / safe_filename

        # Handle duplicate filenames
        counter = 1
        original_filepath = filepath
        while filepath.exists():
            name_part = original_filepath.stem
            ext_part = original_filepath.suffix
            filepath = self.download_dir / f"{name_part}_{counter}{ext_part}"
            counter += 1

        url = f"{self.server_url}/api/assets/{asset_id}/original"

        for attempt in range(MAX_RETRIES):
            try:
                print(f"Downloading: {original_filename}")

                # Download with auth headers but no content-type for binary download
                download_headers = {"X-API-Key": self.headers["X-API-Key"]}
                response = requests.get(
                    url, headers=download_headers, stream=True
                )
                response.raise_for_status()

                # Write file in chunks
                with open(filepath, "wb") as f:
                    for chunk in response.iter_content(chunk_size=8192):
                        if chunk:
                            f.write(chunk)

                file_size = filepath.stat().st_size
                print(f"Downloaded: {filepath.name} ({file_size:,} bytes)")
                return True

            except requests.exceptions.RequestException as e:
                print(
                    f"Attempt {attempt + 1}/{MAX_RETRIES} failed for {original_filename}: {e}"
                )
                if attempt < MAX_RETRIES - 1:
                    time.sleep(RETRY_DELAY)
                else:
                    print(
                        f"Failed to download {original_filename} after {MAX_RETRIES} attempts"
                    )
                    return False

        return False

    def make_safe_filename(self, filename: str) -> str:
        """Make filename safe for filesystem"""
        # Replace problematic characters
        safe_chars = (
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-"
        )
        safe_filename = ""

        for char in filename:
            if char in safe_chars:
                safe_filename += char
            elif char in " ":
                safe_filename += "_"
            else:
                safe_filename += "_"

        # Ensure it doesn't start with a dot
        if safe_filename.startswith("."):
            safe_filename = "_" + safe_filename[1:]

        return safe_filename

    def download_all_trashed_assets(self):
        """Main function to download all trashed assets"""
        print("Starting download of all trashed assets...")

        # Get all trashed assets
        trashed_assets = self.get_all_trashed_assets()

        if not trashed_assets:
            print("No trashed assets found")
            return

        print(
            f"\nStarting download of {len(trashed_assets)} trashed assets..."
        )

        successful_downloads = 0
        failed_downloads = 0

        for i, asset in enumerate(trashed_assets, 1):
            print(f"\n[{i}/{len(trashed_assets)}] ", end="")

            if self.download_asset(asset):
                successful_downloads += 1
            else:
                failed_downloads += 1

        print("\n" + "=" * 50)
        print("Download Summary:")
        print(f"Total assets: {len(trashed_assets)}")
        print(f"Successful downloads: {successful_downloads}")
        print(f"Failed downloads: {failed_downloads}")
        print(f"Download directory: {self.download_dir.absolute()}")
        print("=" * 50)


def main():
    """Main function"""

    # Initialize downloader
    downloader = ImmichTrashDownloader(
        IMMICH_SERVER_URL, API_KEY, DOWNLOAD_DIR
    )

    try:
        downloader.download_all_trashed_assets()
    except KeyboardInterrupt:
        print("\nDownload interrupted by user")
    except Exception as e:
        print(f"Unexpected error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
