/* main.c - 

 * based on Chromatic guitar tuner by Bjorn Roche. Ported to 3ds by Nop90
 *
 * Copyright (C) 2012 by Bjorn Roche
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  This software is provided "as is" without express or
 * implied warranty.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <3ds.h>
#include "libfft.h"

/* -- some basic parameters -- */
#define SAMPLE_RATE (8180)
#define FFT_SIZE (8192)
#define FFT_EXP_SIZE (13)

/* -- functions declared and used here -- */
void buildHammingWindow( float *window, int size );
void buildHanWindow( float *window, int size );
void applyWindow( float *window, float *data, int size );
//a must be of length 2, and b must be of length 3
void computeSecondOrderLowPassParameters( float srate, float f, float *a, float *b );
//mem must be of length 4.
float processSecondOrderFilter( float x, float *mem, float *a, float *b );

static char * NOTES[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

u8 gain;

int main()
{
   u8* screen;
   float a[2], b[3], mem1[4], mem2[4];
   void * fft = NULL;

   char * noteNameTable  = malloc (FFT_SIZE * sizeof(char));
   float * data = malloc (FFT_SIZE * sizeof(float));
   float * datai = malloc (FFT_SIZE * sizeof(float));
   float * window = malloc (FFT_SIZE * sizeof(float));
   float * freqTable = malloc (FFT_SIZE * sizeof(float));
   float * notePitchTable = malloc (FFT_SIZE * sizeof(float));

	gfxInit(GSP_RGBA8_OES,GSP_RGBA8_OES,false);
	consoleInit(GFX_BOTTOM, NULL);


	bool initialized = true;

	u32 micbuf_size = 0x30000;
	s16* micbuf = memalign(0x1000, micbuf_size);

	printf("Initializing MIC...\n");
	if(R_FAILED(micInit(micbuf, micbuf_size)))
	{
		initialized = false;
		printf("Could not initialize MIC.\n");
	} else printf("MIC initialized.\n");

	MICU_GetGain(&gain);
	u32 micbuf_datasize = micGetSampleDataSize();


   buildHanWindow( window, FFT_SIZE );


   fft = initfft( FFT_EXP_SIZE );
   computeSecondOrderLowPassParameters( SAMPLE_RATE, 330, a, b );
   mem1[0] = 0; mem1[1] = 0; mem1[2] = 0; mem1[3] = 0;
   mem2[0] = 0; mem2[1] = 0; mem2[2] = 0; mem2[3] = 0;
   //freq/note tables
   for( int i=0; i<FFT_SIZE; ++i ) {
      freqTable[i] = ( SAMPLE_RATE * i ) / (float) ( FFT_SIZE );
   }
   for( int i=0; i<FFT_SIZE; ++i ) {
      noteNameTable[i] = 255;
      notePitchTable[i] = -1;
   }
   for( int i=0; i<127; ++i ) {
      float pitch = ( 440.0 / 32.0 ) * pow( 2, (i-9.0)/12.0 ) ;
      if( pitch > SAMPLE_RATE / 2.0 )
         break;
      //find the closest frequency using brute force.
      float min = 1000000000.0;
      int index = -1;
      for( int j=0; j<FFT_SIZE; ++j ) {
         if( fabsf( freqTable[j]-pitch ) < min ) {
             min = fabsf( freqTable[j]-pitch );
             index = j;
         }
      }
      noteNameTable[index] = i%12;
      notePitchTable[index] = pitch;
	// output note table - only needed for debug
//      printf( "%f %d %s\n", pitch, index, NOTES[noteNameTable[index]] );
   }

	while(aptMainLoop())
	{
		hidScanInput();
		gspWaitForVBlank();
		screen = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);

		u32 kDown = hidKeysDown();
		if (kDown & KEY_START)
			break; // break in order to return to hbmenu
		if (kDown & KEY_UP)
		{
			if(gain<255) MICU_SetGain(++gain);
		}
		if (kDown & KEY_DOWN)
		{
			if(gain>0) MICU_SetGain(--gain);
		}
		

		if(initialized)
		{
//			if(R_SUCCEEDED(MICU_StartSampling(MICU_ENCODING_PCM16_SIGNED, MICU_SAMPLE_RATE_8180, 0, micbuf_datasize, true)))
			if(R_SUCCEEDED(MICU_StartSampling(MICU_ENCODING_PCM16_SIGNED, MICU_SAMPLE_RATE_8180, 0, FFT_SIZE, true)))

			{


			  // convert s16 in float in range -a to 1
			  for( int i=0; i<FFT_SIZE; ++i )
				 data[i] = ((float) micbuf[i] )/32768.0;
			  // low-pass
			  for( int j=0; j<FFT_SIZE; ++j ) {
				 data[j] = processSecondOrderFilter( data[j], mem1, a, b );
				 data[j] = processSecondOrderFilter( data[j], mem2, a, b );
			  }

			  // window
			  applyWindow( window, data, FFT_SIZE );

			  // do the fft
			  for( int j=0; j<FFT_SIZE; ++j )
				 datai[j] = 0;
			  applyfft( fft, data, datai, false );

			  //find the peak
			  float maxVal = -1;
			  int maxIndex = -1;
			  for( int j=0; j<FFT_SIZE/2; ++j ) {
				 float v = data[j] * data[j] + datai[j] * datai[j] ;
				 if( v > maxVal ) {
					maxVal = v;
					maxIndex = j;
				 }
			  }
			  float freq = freqTable[maxIndex];
			  //find the nearest note:
			  int nearestNoteDelta=0;
			  while( true ) {
				 if( nearestNoteDelta < maxIndex && noteNameTable[maxIndex-nearestNoteDelta] != 255 ) {
					nearestNoteDelta = -nearestNoteDelta;
					break;
				 } else if( nearestNoteDelta + maxIndex < FFT_SIZE && noteNameTable[maxIndex+nearestNoteDelta] != 255 ) {
					break;
				 }
				 ++nearestNoteDelta;
			  }
			  char nearestNoteName = noteNameTable[maxIndex+nearestNoteDelta];
			  float nearestNotePitch = notePitchTable[maxIndex+nearestNoteDelta];
			  float centsSharp = 1200 * log( freq / nearestNotePitch ) / log( 2.0 );

			  // now output the results:
		      printf("\x1b[2J"); //clear screen
		 
			  printf( "Tuner listening. Press START to exit.\n\n" );
			  printf( "Mic gain: %i - mic buf data size: %i\n", gain, micbuf_datasize);
			  printf( "%f Hz, %d : %f\n", freq, maxIndex, maxVal*1000 );
			  printf( "Nearest Note: %s\n", NOTES[nearestNoteName] );
			  if( nearestNoteDelta != 0 ) {
				 if( centsSharp > 0 )
					printf( "%f cents sharp.\n", centsSharp );
				 if( centsSharp < 0 )
					printf( "%f cents flat.\n", -centsSharp );
			  } else {
				 printf( "in tune!\n" );
			  }
			  printf( "\n\n" );
			  int chars = 17;//30;
			  if( nearestNoteDelta == 0 || centsSharp >= 0 ) {
				 for( int i=0; i<chars; ++i )
					printf( " " );
			  } else {
				 for( int i=0; i<chars+centsSharp; ++i )
					printf( " " );
				 for( int i=chars+centsSharp<0?0:chars+centsSharp; i<chars; ++i )
					printf( "=" );
			  }
			  printf( " %2s ", NOTES[nearestNoteName] );
			  if( nearestNoteDelta != 0 )
				 for( int i=0; i<chars && i<centsSharp; ++i )
				   printf( "=" );
			  printf("\n");
			  
			for(int i=0;i<400;i++){
				int h = (int)(240 * 10000 * (data[i*2] * data[i*2] + datai[i*2] * datai[i*2]));
				if(h>240) h=240;
				for (int y = 0; y<240 ; y++) 
					screen[240*4*i+4*y+2]=(y<h)?255:0;
				}
			}
		}

		gfxFlushBuffers();
		gfxSwapBuffers();
	}

	micExit();
	free(micbuf);

	free(noteNameTable);
	free(data);
	free(datai);
	free(window);
	free(freqTable);
	free(notePitchTable);

	gfxExit();
	return 0;
}

void buildHammingWindow( float *window, int size )
{
   for( int i=0; i<size; ++i )
      window[i] = .54 - .46 * cos( 2 * M_PI * i / (float) size );
}
void buildHanWindow( float *window, int size )
{
   for( int i=0; i<size; ++i )
      window[i] = .5 * ( 1 - cos( 2 * M_PI * i / (size-1.0) ) );
}
void applyWindow( float *window, float *data, int size )
{
   for( int i=0; i<size; ++i )
      data[i] *= window[i] ;
}
void computeSecondOrderLowPassParameters( float srate, float f, float *a, float *b )
{
   float a0;
   float w0 = 2 * M_PI * f/srate;
   float cosw0 = cos(w0);
   float sinw0 = sin(w0);
   //float alpha = sinw0/2;
   float alpha = sinw0/2 * sqrt(2);

   a0   = 1 + alpha;
   a[0] = (-2*cosw0) / a0;
   a[1] = (1 - alpha) / a0;
   b[0] = ((1-cosw0)/2) / a0;
   b[1] = ( 1-cosw0) / a0;
   b[2] = b[0];
}
float processSecondOrderFilter( float x, float *mem, float *a, float *b )
{
    float ret = b[0] * x + b[1] * mem[0] + b[2] * mem[1]
                         - a[0] * mem[2] - a[1] * mem[3] ;

		mem[1] = mem[0];
		mem[0] = x;
		mem[3] = mem[2];
		mem[2] = ret;

		return ret;
}