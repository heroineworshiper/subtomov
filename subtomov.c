/*
 *  Sections come from:
 *  VobSub2SRT
 *  Copyright (C) 2010-2016 Rüdiger Sonderfeld <ruediger@c-plusplus.de>
 *
 *  subtomov 
 *  Copyright (C) 2024 Adam Williams <broadcast@earthling.net>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include "vobsub.h"
#include "spudec.h"

typedef void* spu_t;
typedef void* vob_t;

#define PTS_TO_S 90000
// rgb chroma key
#define CHROMA_R 0
#define CHROMA_G 0
#define CHROMA_B 0
#define ON_TOP
#define FPS 4

int global_w, global_h;
uint8_t *output;
void clear_output()
{
    int i;
    for(i = 0; i < global_h * global_w; i++)
    {
        output[i * 3 + 0] = CHROMA_R;
        output[i * 3 + 1] = CHROMA_G;
        output[i * 3 + 2] = CHROMA_B;
    }
}

int main(int argc, char **argv) 
{
    char *basepath = 0;
    char *outpath = 0;
    int i, j;

	if(argc < 2)
	{
		fprintf(stderr, "Subtitle renderer\n"
			"Usage: subtomov <input base path> <output path>\n"
			"Example: mpeg3subs english subs.mp4\n");
		exit(1);
    }

	basepath = argv[1];
    outpath = argv[2];


    spu_t spu;
    vob_t vob = vobsub_open(basepath, 
        0, // ifo path
        1, // force
        0, // y_threshold
        &spu);
    spudec_globals(spu, &global_w, &global_h);
    printf("main %d: w=%d h=%d\n", __LINE__, global_w, global_h);

// the output video encoding
    output = malloc(global_w * global_h * 3);
// the 1st subtitle is blank
    clear_output();
    char string[1024];
    sprintf(string, 
        "ffmpeg -y -f rawvideo -y -pix_fmt rgb24 -r %d -s:v %dx%d -i - -c:v mpeg4 -qscale:v 5 -pix_fmt yuvj420p -an %s",
        FPS,
        global_w,
        global_h,
        outpath);
    printf("main %d: running %s\n", __LINE__, string);
    FILE *ffmpeg_writer = popen(string, "w");

    void *packet;
    int timestamp;
    int len;
    unsigned int last_start_pts = 0;
    int min_width = 9;
    int min_height = 1;
    unsigned int current_pts = 0;
    unsigned int end_pts = 0;
    while((len = vobsub_get_next_packet(vob, &packet, &timestamp)) > 0) 
    {
        if(timestamp >= 0) 
        {
            spudec_assemble(spu, (unsigned char*)packet, len, timestamp);
            spudec_heartbeat(spu, timestamp);
            unsigned char const *image;
            unsigned char const *aimage;
            size_t image_size;
            unsigned int x1, y1, x2, y2;
            unsigned int width, height, stride, start_pts;
            spudec_get_data(spu, 
                &image, 
                &aimage,
                &image_size, 
                &x1,
                &y1,
                &width, 
                &height, 
                &stride, 
                &start_pts, 
                &end_pts);
            // skip this packet if it is another packet of a subtitle that
            // was decoded from multiple mpeg packets.
            if (start_pts == last_start_pts) continue;
            last_start_pts = start_pts;

            x2 = x1 + width;
            y2 = y1 + height;

            printf("main %d: got x1=%d y1=%d x2=%d y2=%d start_pts=%f end_pts=%f\n", 
                __LINE__, 
                x1,
                y1,
                x2, 
                y2,
                (double)start_pts / PTS_TO_S,
                (double)end_pts / PTS_TO_S);

            if(width < min_width || height < min_height) 
            {
                printf("main %d: too small %dx%d\n", __LINE__, width, height);
                continue;
            }

// print subtitles on the top of the output & place the output below the video
#ifdef ON_TOP
            y1 = 0;
            y2 = height;
#endif

//            for(i = 0; i < width * height; i++)
//                printf("%02x ", aimage[i]);
//            printf("\n");
//            exit(0);

// fill the previous subtitle until the current time
            while(current_pts < start_pts)
            {
                if(ffmpeg_writer) fwrite(output, global_w * global_h * 3, 1, ffmpeg_writer);
                current_pts += PTS_TO_S / FPS;
            }

// overlay the subtitle
            clear_output();



	        for(i = y1; 
		        i < y2 && i < global_h; 
		        i++)
            {
		        unsigned char *output_row = output + 
			        (i * global_w + x1) * 3;
		        const unsigned char *input_r = image + (i - y1) * stride; // 0-255
		        const unsigned char *input_g = image + (i - y1) * stride; // 0-255
		        const unsigned char *input_b = image + (i - y1) * stride; // 0-255
		        const unsigned char *input_a = aimage + (i - y1) * stride; // 0-1

		        for(j = x1; 
			        j < x2 && j < global_w; 
			        j++)
                {
                    int inv_a = 1 - *input_a;
                    output_row[0] = *input_r * *input_a + CHROMA_R * inv_a;
                    output_row[1] = *input_g * *input_a + CHROMA_G * inv_a;
                    output_row[2] = *input_b * *input_a + CHROMA_B * inv_a;
                    output_row += 3;
                    input_r++;
                    input_g++;
                    input_b++;
                    input_a++;
                }
            }
        }
    }

// fill the last subtitle
    while(current_pts < end_pts)
    {
        if(ffmpeg_writer) fwrite(output, global_w * global_h * 3, 1, ffmpeg_writer);
        current_pts += PTS_TO_S / FPS;
    }
}




