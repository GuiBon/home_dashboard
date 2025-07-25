# Home Dashboard v2

A C-based home dashboard application that displays weather, menu, and calendar information on an e-ink display with real-time updates.

## Features

- **Weather Display**: Real-time weather data with 12-hour forecast
- **Menu Management**: Daily meal planning with Google Sheets integration
- **Calendar Integration**: iCal calendar events display
- **E-ink Display**: Optimized for Waveshare 7.5" e-Paper display
- **Intelligent Refresh**: Fast refresh for weather updates, full refresh for menu/calendar changes
- **Multi-threaded**: Separate threads for clock, weather, menu, and calendar updates
- **Logging System**: Comprehensive logging with debug/production modes
- **Graceful Degradation**: Continues operation even if some data sources fail

## Hardware Requirements

- Raspberry Pi (or compatible ARM/x86 Linux system)
- Waveshare 7.5" e-Paper display (V2)
- Internet connection

## Software Dependencies

### System Packages

Install required system packages:

```bash
make install-deps
```

Or manually:

```bash
sudo apt-get update
sudo apt-get install libcurl4-openssl-dev libcjson-dev libcairo2-dev libfreetype6-dev
```

### Waveshare e-Paper Library

Download and extract the Waveshare e-Paper library:

```bash
# Download the library (adjust URL for latest version)
wget https://github.com/waveshareteam/e-Paper/archive/refs/heads/master.zip -O waveshare-epaper.zip

# Extract to lib/ directory
unzip waveshare-epaper.zip
mv e-Paper-master lib

# Clean up
rm waveshare-epaper.zip
```

The `lib/` directory should contain the Waveshare e-Paper library files needed for display functionality.

## Configuration

### 1. Google Sheets API (Menu)

Create `config/credentials.json` with your Google Sheets API credentials:

```json
{
  "type": "service_account",
  "project_id": "your-project-id",
  "private_key_id": "your-key-id",
  "private_key": "-----BEGIN PRIVATE KEY-----\n...\n-----END PRIVATE KEY-----\n",
  "client_email": "your-service-account@your-project.iam.gserviceaccount.com",
  "client_id": "your-client-id",
  "auth_uri": "https://accounts.google.com/o/oauth2/auth",
  "token_uri": "https://oauth2.googleapis.com/token"
}
```

### 2. Environment Variables

Create a `.env` file in the project root:

```bash
# Dashboard Configuration
DASHBOARD_SPREADSHEET_ID=your-google-sheets-id
DASHBOARD_ICAL_URL=https://your-calendar-url/ical
```

The application automatically loads these variables at startup. System environment variables take priority over `.env` file values.

### 3. Update Configuration

Edit `src/common.h` to configure:

- Weather location (latitude/longitude)  
- Display settings

## Building

```bash
# Build the application
make

# Clean build files
make clean

# Run in debug mode (console output)
make test
```

The build directory will be created automatically if it doesn't exist.

## Usage

### Debug Mode (Console Output)
```bash
./build/dashboard --debug
```

### Production Mode (E-ink Display)
```bash
./build/dashboard
```

### Command Line Options
- `--debug`: Run once in debug mode with console output
- `--date DD/MM/YYYY`: Override today's date for testing
- `--help`: Show help message

### Examples
```bash
# Test with specific date
./build/dashboard --debug --date 25/12/2024

# Normal operation
./build/dashboard
```

## Update Schedules

- **Clock**: Updates every minute
- **Weather**: Updates every 10 minutes (XX:X0:00)
- **Menu**: Updates daily at midnight (00:00:00)
- **Calendar**: Updates hourly (XX:00:00)

## Project Structure

```
├── src/                    # Source code
│   ├── main.c             # Main orchestrator
│   ├── weather.c          # Weather API integration
│   ├── menu.c             # Google Sheets menu integration
│   ├── calendar.c         # iCal calendar integration
│   ├── display_*.c        # Display modules
│   ├── http.c             # HTTP client utilities
│   └── logging.c          # Logging system
├── config/                # Configuration files
│   ├── credentials.json   # Google API credentials (excluded from git)
│   └── fonts/            # Display fonts
├── scripts/               # Python display scripts
├── lib/                   # Waveshare e-Paper library (excluded from git)
├── build/                 # Build output (created automatically, excluded from git)
├── .env                   # Environment variables (excluded from git)
└── log/                   # Application logs (excluded from git)
```

## Logging

Logs are written to `log/dashboard.log` with timestamps. Log levels:
- **DEBUG**: Detailed operation information
- **INFO**: General status updates and successful operations
- **ERROR**: Error conditions and failures

Success messages are logged when data is updated:
- ✅ Weather data updated successfully
- ✅ Menu data updated successfully  
- ✅ Calendar data updated successfully

## Troubleshooting

### Common Issues

1. **Missing environment variables**
   ```bash
   # Check if .env file exists and contains required variables
   cat .env
   
   # Create .env file if missing
   echo "DASHBOARD_SPREADSHEET_ID=your-spreadsheet-id" > .env
   echo "DASHBOARD_ICAL_URL=your-calendar-url" >> .env
   ```

2. **Build fails with missing dependencies**
   ```bash
   make install-deps
   ```

3. **Permission denied on GPIO**
   ```bash
   sudo usermod -a -G gpio $USER
   # Then logout/login
   ```

4. **Display not updating**
   - Check GPIO connections
   - Verify e-ink display Python scripts are working
   - Check logs in `log/dashboard.log`

5. **API failures**
   - Verify internet connection
   - Check API credentials in `config/credentials.json`
   - Ensure `.env` file contains correct values

### Debug Mode

Run with `--debug` flag to:
- Display output to console instead of e-ink
- Generate `dashboard_debug.png` for visual debugging
- See detailed logging information

## Development

### Adding New Features

1. Update relevant source files in `src/`
2. Add any new dependencies to Makefile
3. Test in debug mode first
4. Update this README

### Code Style

- English comments and log messages only
- French text preserved for user-facing display elements
- Comprehensive error handling
- Memory management with proper cleanup

## License

This project uses the Waveshare e-Paper library. Please refer to their licensing terms for the display components.