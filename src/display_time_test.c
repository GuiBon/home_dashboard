#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "EPD_7in5_V2.h"
#include "GUI_Paint.h"
#include "GUI_BMPfile.h"
#include "DEV_Config.h"

int main(void) {
    printf("EPD_7IN5_V2_test Demo\r\n");
    if(DEV_Module_Init()!=0){
        return -1;
    }

    printf("e-Paper Init and Clear...\r\n");
    EPD_7IN5_V2_Init();

    struct timespec start={0,0}, finish={0,0}; 
    clock_gettime(CLOCK_REALTIME,&start);
    EPD_7IN5_V2_Clear();
    clock_gettime(CLOCK_REALTIME,&finish);
    printf("%ld S\r\n",finish.tv_sec-start.tv_sec);
    DEV_Delay_ms(500);
	
    //Create a new image cache
    UBYTE *BlackImage;
    /* you have to edit the startup_stm32fxxx.s file and set a big enough heap size */
    UDOUBLE Imagesize = ((EPD_7IN5_V2_WIDTH % 8 == 0)? (EPD_7IN5_V2_WIDTH / 8 ): (EPD_7IN5_V2_WIDTH / 8 + 1)) * EPD_7IN5_V2_HEIGHT;
    if((BlackImage = (UBYTE *)malloc(Imagesize)) == NULL) {
        printf("Failed to apply for black memory...\r\n");
        return -1;
    }
    printf("Paint_NewImage\r\n");
    Paint_NewImage(BlackImage, EPD_7IN5_V2_WIDTH, EPD_7IN5_V2_HEIGHT, 0, WHITE);

    EPD_7IN5_V2_Init_Part();
	// With ROTATE_270, swap width and height to accommodate rotated text
	Paint_NewImage(BlackImage, Font20.Height, Font20.Width * 8, ROTATE_270, WHITE);
    Debug("Partial refresh\r\n");
    Paint_SelectImage(BlackImage);
    Paint_Clear(WHITE);
	
    PAINT_TIME sPaint_time;
    sPaint_time.Hour = 12;
    sPaint_time.Min = 34;
    sPaint_time.Sec = 56;
    UBYTE num = 10;
    for (;;) {
        // Get current time
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        if (!tm_info) {
            return -1;
        }
    
        // Format time string as "HH:MM"
        char time_str[9];
        snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

        Paint_ClearWindows(0, 0, Font20.Width * 8, Font20.Height, BLACK);
        Paint_DrawString_EN(0, 0, &time_str, &Font20, BLACK, WHITE);
        // Paint_DrawTime(0, 0, &sPaint_time, &Font20, BLACK, WHITE);

        num = num - 1;
        if(num == 0) {
            break;
        }
		// With ROTATE_270, coordinates need adjustment for portrait viewing
		// Time should appear at top of portrait display
		EPD_7IN5_V2_Display_Part(BlackImage, 100, 200, 100 + Font20.Height, 200 + Font20.Width * 8);
        DEV_Delay_ms(500);//Analog clock 1s
    }

    printf("Clear...\r\n");
    EPD_7IN5_V2_Init();
    EPD_7IN5_V2_Clear();

    printf("Goto Sleep...\r\n");
    EPD_7IN5_V2_Sleep();
    // free(BlackImage);
    // BlackImage = NULL;
    DEV_Delay_ms(2000);//important, at least 2s
    // close 5V
    printf("close 5V, Module enters 0 power consumption ...\r\n");
    DEV_Module_Exit();
    
    return 0;
}