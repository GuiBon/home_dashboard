#!/usr/bin/env python3
"""
Dedicated menu fetcher script for the home dashboard.
Replaces inline Python code execution with a proper script.
"""

import argparse
import json
import sys
import os
from datetime import datetime

try:
    from menu import GoogleSheetsMenuParser
except ImportError as e:
    print(json.dumps({"error": f"Failed to import menu module: {e}"}), file=sys.stderr)
    sys.exit(1)

def parse_date_string(date_str):
    """Parse date string in DD/MM/YYYY format."""
    try:
        parts = date_str.split('/')
        if len(parts) == 2:
            # DD/MM format, assume current year
            day, month = int(parts[0]), int(parts[1])
            year = datetime.now().year
        elif len(parts) == 3:
            # DD/MM/YYYY format
            day, month, year = int(parts[0]), int(parts[1]), int(parts[2])
        else:
            raise ValueError("Invalid date format")
        
        return datetime(year, month, day)
    except (ValueError, IndexError) as e:
        raise ValueError(f"Invalid date format '{date_str}'. Expected DD/MM or DD/MM/YYYY") from e

def get_menu_data(spreadsheet_id, credentials_file, test_date=None):
    """Fetch menu data using GoogleSheetsMenuParser."""
    try:
        parser = GoogleSheetsMenuParser(credentials_file, spreadsheet_id)
        
        if test_date:
            # Calculate today and tomorrow from test_date
            from datetime import timedelta
            today = test_date
            tomorrow = test_date + timedelta(days=1)
            target_dates = [today, tomorrow]
        else:
            # Use current date
            from datetime import datetime, timedelta
            today = datetime.now()
            tomorrow = today + timedelta(days=1)
            target_dates = [today, tomorrow]
        
        # get_menus_for_dates expects a list of datetime objects
        raw_menus = parser.get_menus_for_dates(target_dates)
        
        # Convert to the expected format
        today_key = target_dates[0].strftime('%Y-%m-%d')
        tomorrow_key = target_dates[1].strftime('%Y-%m-%d')
        
        menus = {
            "today": {
                "date": target_dates[0].strftime('%A %d/%m/%Y'),
                "midi": raw_menus.get(today_key, {}).get('midi', ''),
                "soir": raw_menus.get(today_key, {}).get('soir', '')
            },
            "tomorrow": {
                "date": target_dates[1].strftime('%A %d/%m/%Y'),
                "midi": raw_menus.get(tomorrow_key, {}).get('midi', ''),
                "soir": raw_menus.get(tomorrow_key, {}).get('soir', '')
            }
        }
        
        return menus
    except Exception as e:
        raise RuntimeError(f"Failed to fetch menu data: {e}") from e

def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="Fetch menu data from Google Sheets",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --spreadsheet-id "1y-6rNDrafR7s..." --credentials "./credentials.json"
  %(prog)s --spreadsheet-id "1y-6rNDrafR7s..." --date "17/07/2025"
        """
    )
    
    parser.add_argument(
        '--spreadsheet-id',
        required=True,
        help='Google Sheets spreadsheet ID'
    )
    
    parser.add_argument(
        '--credentials',
        default='./credentials.json',
        help='Path to Google service account credentials file (default: ./credentials.json)'
    )
    
    parser.add_argument(
        '--date',
        help='Specific date to fetch menus for (format: DD/MM or DD/MM/YYYY). If not specified, fetches today/tomorrow.'
    )
        
    args = parser.parse_args()
    
    try:
        # Parse date if provided
        test_date = None
        if args.date:
            test_date = parse_date_string(args.date)
        
        # Check credentials file exists
        if not os.path.isfile(args.credentials):
            raise FileNotFoundError(f"Credentials file not found: {args.credentials}")
        
        # Fetch menu data        
        menus = get_menu_data(
            args.spreadsheet_id,
            args.credentials,
            test_date
        )
        
        # Output JSON to stdout (ensure_ascii=False for French characters)
        print(json.dumps(menus, ensure_ascii=False))
    
    except Exception as e:
        # Output error to stderr and exit with non-zero code
        error_response = {
            "error": str(e),
            "success": False
        }
        print(json.dumps(error_response), file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()