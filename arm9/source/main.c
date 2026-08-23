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

	char vidName[32];
	sprintf(vidName, "/DCIM/100DSI00/VID_%04d.BIN", getVideoNumber());
	FILE *out = fopen(vidName, "wb");

	printf("\nIt's recording!\n");
	printf("START to finish\n\n");
	printf("Output file:\n%s\n", vidName);

	// Allocate frame buffer on heap (256×192×2 bytes)
	u16 *framebuf = (u16 *)malloc(256 * 192 * 2);
	if(framebuf == NULL) {
		printf("ERROR: Failed to allocate frame buffer!\n");
		printf("Out of memory.\n");
		cameraDeactivate(camera);
		fclose(out);
		return 0;
	}

	cpuStartTiming(0);
	uint32_t frameCount = 0;

	while(1) {
		swiWaitForVBlank();

		// Capture frame into heap buffer instead of VRAM
		cameraTransferStart(framebuf, CAPTURE_MODE_CAPTURE);
		while(cameraTransferActive())
			swiDelay(100);

		// Write frame data to file
		size_t written = fwrite(framebuf, 1, 256 * 192 * 2, out);
		if(written != 256 * 192 * 2) {
			printf("ERROR: File write failed at frame %u\n", frameCount);
			printf("Written: %u bytes, expected: %u\n", (unsigned int)written, 256 * 192 * 2);
			cameraDeactivate(camera);
			fclose(out);
			free(framebuf);
			return 0;
		}

		// Write frame timing metadata
		u32 time = cpuGetTiming();
		if(fwrite(&time, 4, 1, out) != 1) {
			printf("ERROR: Timing write failed at frame %u\n", frameCount);
			cameraDeactivate(camera);
			fclose(out);
			free(framebuf);
			return 0;
		}
		cpuStartTiming(0);
		frameCount++;

		scanKeys();
		if(keysDown() & KEY_START) {
			printf("\nRecording stopped.\n");
			printf("Frames recorded: %u\n", frameCount);
			// Disable camera so the light turns off
			cameraDeactivate(camera);
			fclose(out);
			free(framebuf);

			return 0;
		}
	}
}
