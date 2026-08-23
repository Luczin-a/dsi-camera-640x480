#include "camera.h"
#include "version.h"

#include <dirent.h>
#include <fat.h>
#include <nds.h>
#include <stdio.h>
#include <stdlib.h>

int getVideoNumber() {
	int highest = -1;

	DIR *pdir = opendir("/DCIM/100DSI00");
	if(pdir == NULL) {
		printf("Unable to open directory");
		return -1;
	} else {
		while(true) {
			struct dirent *pent = readdir(pdir);
			if(pent == NULL)
				break;

			if(strncmp(pent->d_name, "VID_", 4) == 0) {
				int val = atoi(pent->d_name + 4);
				if(val > highest)
					highest = val;
			}
		}
		closedir(pdir);
	}

	return highest + 1;
}

int main(int argc, char **argv) {
	consoleDemoInit();
	vramSetBankA(VRAM_A_MAIN_BG);
	videoSetMode(MODE_5_2D);
	int bg3Main = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 1, 0);

	printf("dsi-camcorder " VER_NUMBER "\n");

	bool fatInited = fatInitDefault();
	if(fatInited) {
		mkdir("/DCIM", 0777);
		mkdir("/DCIM/100DSI00", 0777);
	} else {
		printf("FAT init failed!\n");
		do {
			swiWaitForVBlank();
			scanKeys();
		} while(!(keysDown() & KEY_START));
		return 0;
	}

	printf("Initializing...\n");
	pxiWaitRemote(PXI_CAMERA); // Wait for ARM7 to initialize PXI
	cameraInit();

	Camera camera = CAM_OUTER;
	cameraActivate(camera);

	printf("\nL/R to start recording\n");
	printf("START to finish\n");

	while(1) {
		u16 pressed;
		do {
			swiWaitForVBlank();
			if(!cameraTransferActive())
				cameraTransferStart(bgGetGfxPtr(bg3Main), CAPTURE_MODE_PREVIEW);
			scanKeys();
			pressed = keysDown();
		} while(!pressed);

		if(pressed & KEY_A) {
			// Wait for previous transfer to finish
			while(cameraTransferActive())
				swiWaitForVBlank();
			cameraTransferStop();

			// Switch camera
			camera = camera == CAM_INNER ? CAM_OUTER : CAM_INNER;
			cameraActivate(camera);

			printf("Swapped to %s camera\n", camera == CAM_INNER ? "inner" : "outer");
		} else if(fatInited && pressed & (KEY_L | KEY_R)) {
			printf("Recording...\n");

			// Wait for previous transfer to finish
			while(cameraTransferActive())
				swiWaitForVBlank();
			cameraTransferStop();

			char vidName[32];
			sprintf(vidName, "/DCIM/100DSI00/VID_%04d.BIN", getVideoNumber());
			FILE *out = fopen(vidName, "wb");

			if(out == NULL) {
				printf("ERROR: Failed to open file!\n");
				cameraDeactivate(camera);
				return 0;
			}

			// Allocate frame buffer for 640x480
			u16 *framebuf = (u16 *)malloc(640 * 480 * sizeof(u16));
			if(framebuf == NULL) {
				printf("ERROR: Failed to allocate!\n");
				cameraDeactivate(camera);
				fclose(out);
				return 0;
			}

			printf("Buffer allocated, switching mode...\n");

			// Switch to capture mode
			cameraTransferStart(framebuf, CAPTURE_MODE_CAPTURE);
			while(cameraTransferActive())
				swiWaitForVBlank();

			printf("Recording started (press START)\n");

			uint32_t frameCount = 0;

			while(1) {
				swiWaitForVBlank();

				// Capture frame
				cameraTransferStart(framebuf, CAPTURE_MODE_CAPTURE);
				while(cameraTransferActive())
					swiDelay(100);

				// Write to file
				size_t written = fwrite(framebuf, 1, 640 * 480 * 2, out);
				if(written != 640 * 480 * 2) {
					printf("Write error at frame %u\n", frameCount);
					break;
				}

				frameCount++;
				if(frameCount % 10 == 0)
					printf("Frames: %u\n", frameCount);

				scanKeys();
				if(keysDown() & KEY_START) {
					break;
				}
			}

			printf("Recording stopped.\n");
			printf("Frames: %u\n", frameCount);
			
			free(framebuf);
			fclose(out);

			// Return to preview mode
			cameraTransferStart(bgGetGfxPtr(bg3Main), CAPTURE_MODE_PREVIEW);
		} else if(pressed & KEY_START) {
			// Disable camera so the light turns off
			cameraDeactivate(camera);

			return 0;
		}
	}
}
