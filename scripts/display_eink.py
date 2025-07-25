#!/usr/bin/env python3
"""
E-ink display script for home dashboard
Displays a PNG image on Waveshare 7.5" e-paper display
"""

import sys
import os
from PIL import Image
import logging

# Set up logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

def display_image_on_eink(image_path, fast_refresh=False):
    """
    Display a PNG image on the Waveshare 7.5" e-ink display
    
    Args:
        image_path (str): Path to the PNG image file
        fast_refresh (bool): Use fast refresh mode (faster but may have ghosting)
        
    Returns:
        bool: True if successful, False otherwise
    """
    try:
        # Import Waveshare library
        try:
            import sys
            sys.path.append('/home/guillaume/home_dashboard/lib/e-Paper/RaspberryPi_JetsonNano/python/lib')
            from waveshare_epd import epd7in5_V2
        except ImportError as e:
            logger.error(f"Failed to import Waveshare library: {e}")
            logger.error("Make sure the Waveshare e-Paper library is installed")
            return False
        
        # Check if image file exists
        if not os.path.exists(image_path):
            logger.error(f"Image file not found: {image_path}")
            return False
        
        logger.info(f"Loading image: {image_path}")
        
        # Load and process image
        image = Image.open(image_path)
        
        # Convert to 1-bit mode for e-ink display
        if image.mode != '1':
            logger.info("Converting image to 1-bit mode for e-ink display")
            image = image.convert('1')
        
        # Resize if necessary (Waveshare 7.5" is 800x480)
        target_size = (480, 800)
        if image.size != target_size:
            logger.info(f"Resizing image from {image.size} to {target_size}")
            image = image.resize(target_size, Image.Resampling.LANCZOS)
        
        # Initialize e-paper display
        epd = epd7in5_V2.EPD()
        
        if fast_refresh:
            logger.info("Initializing e-paper display (fast refresh mode)...")
            epd.init_fast()
            
            # Display the image with fast refresh
            logger.info("Displaying image on e-ink screen (fast refresh)...")
            epd.display(epd.getbuffer(image))
        else:
            logger.info("Initializing e-paper display (full refresh mode)...")
            epd.init()
            epd.Clear()
            
            # Display the image with full refresh
            logger.info("Displaying image on e-ink screen (full refresh)...")
            epd.display(epd.getbuffer(image))
        
        # Put display to sleep to save power
        logger.info("Putting display to sleep...")
        epd.sleep()
        
        logger.info("✅ Image displayed successfully on e-ink screen")
        return True
        
    except Exception as e:
        logger.error(f"❌ Error displaying image on e-ink: {e}")
        return False

def main():
    """Main entry point"""
    if len(sys.argv) < 2 or len(sys.argv) > 3:
        print("Usage: python3 display_eink.py <image_path> [fast]")
        print("Examples:")
        print("  python3 display_eink.py dashboard_temp.png         # Full refresh")
        print("  python3 display_eink.py dashboard_temp.png fast    # Fast refresh")
        sys.exit(1)
    
    image_path = sys.argv[1]
    fast_refresh = len(sys.argv) == 3 and sys.argv[2].lower() == "fast"
    
    if display_image_on_eink(image_path, fast_refresh):
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()