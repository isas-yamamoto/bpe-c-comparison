/*
Bit plane encoder
Please note:
(1)	Before you download and use the program, you must read and agree the license agreement carefully.
(2)	We supply the source code and program WITHOUT ANY WARRANTIES. The users will be responsible
        for any loses or damages caused by the use of the source code and the program.

Author:
Hongqiang Wang
Department of Electrical Engineering
University of Nebraska-Lincoln
Email: hqwang@bigred.unl.edu, hqwang@eecomm.unl.edu

Your comment and suggestions are welcome. Please report bugs to me via email and I would greatly appreciate it.
Nov. 3, 2006
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"


extern void AdjustOutPut(StructCodingPara * PtrCoding, BitPlaneBits * BlockCodingInfo);

extern void CoeffDegroup(int **img_wav,  int rows,	 int cols);

extern void CoeffDegroupFloating(float **img_wav,  int rows,  int cols);

extern void HeaderReadin(StructCodingPara *PtrCoding);

extern void DWT_Reverse(int **block,  StructCodingPara *PtrCoding);

extern void DWT_ReverseFloating(float **block,
				 StructCodingPara *PtrCoding);

extern short DCDeCoding(StructCodingPara *PtrCoding,
						StructFreBlockString * ,
						BitPlaneBits *BlockInfo);


extern void  ACBpeDecoding(StructCodingPara *PtrCoding,
					 BitPlaneBits *BlockCodingInfo);


short ImageWrite(StructCodingPara *StrPtr, int **image);

short ImageWrite(StructCodingPara *StrPtr,  int **image)
{
	UINT32 r = 0;
	UINT32 i = 0;
	FILE *outfile = NULL;
	UCHAR8 machineendianness;  // indicates endian-ness of the computer -- bug fix (Kiely)
	unsigned long int bigendtest = 1;  //  bug fix (Kiely)
	UINT32 actual_rows;
	if (StrPtr->ImageRows >= StrPtr->PtrHeader->Header.Part1.PadRows_3Bits) {
		actual_rows = StrPtr->ImageRows - StrPtr->PtrHeader->Header.Part1.PadRows_3Bits;
	} else {
		// ヘッダが不正でパディング行数が画像行数より多い場合、行数を0として扱う
		actual_rows = 0;
		fprintf(stderr, "Warning: Corrupted header. PadRows (%u) > ImageRows (%u). Outputting 0 rows.\n",
		        StrPtr->PtrHeader->Header.Part1.PadRows_3Bits, StrPtr->ImageRows);
	}

	if((outfile = fopen(StrPtr->CodingOutputFile,"wb")) == NULL)
		ErrorMsg(BPE_FILE_ERROR);

	StrPtr->ImageRows = StrPtr->ImageRows - StrPtr->PtrHeader->Header.Part1.PadRows_3Bits;

	if(StrPtr->PtrHeader->Header.Part4.PixelBitDepth_4Bits <= 8
		&& StrPtr->PtrHeader->Header.Part4.PixelBitDepth_4Bits != 0)
	{
		if(StrPtr->PtrHeader->Header.Part4.SignedPixels == FALSE) // unsigned image
		{
			UCHAR8 *temp;
			temp = (UCHAR8 *)calloc(sizeof(UCHAR8), StrPtr->ImageWidth);

			for(r = 0; r< StrPtr->ImageRows; r++)
			{
				for(i = 0; i < StrPtr->ImageWidth; i++)
				{
					image[r][i] = (image[r][i] > 0xFF)?0xFF :image[r][i] ;
					image[r][i] = (image[r][i] < 0)?0 :image[r][i] ;
					temp[i] = (UCHAR8)image[r][i];
				}
				fwrite(temp, StrPtr->ImageWidth, sizeof(char),outfile);
			}
			free(temp);
		}
		else
		{
			char *temp;
			temp = (char *)calloc(sizeof(char), StrPtr->ImageWidth);

			for(r=0; r< actual_rows; r++)
			{
				for(i = 0; i < StrPtr->ImageWidth; i++)
				{
					image[r][i] = (image[r][i] > 127)?127 :image[r][i] ;
					image[r][i] = (image[r][i] <-128 )?-128 :image[r][i] ;
					temp[i] = (char)image[r][i];
				}
				fwrite(temp, StrPtr->ImageWidth, sizeof(char),outfile);
			}
			free(temp);
		}
	}
	else if(StrPtr->PtrHeader->Header.Part4.PixelBitDepth_4Bits == 0 ||
		StrPtr->PtrHeader->Header.Part4.PixelBitDepth_4Bits <= 15) // it is 16 bits
	{
		// machineendianness will be 1 if computer is big-endian, or 0 if little-endian
		const unsigned short MSBmask=0xFF00;  //  bug fix (Kiely)
		machineendianness = (((char *)&bigendtest)[0]==0);  //  bug fix (Kiely)

		if(StrPtr->PtrHeader->Header.Part4.SignedPixels == FALSE) // unsigned image
		{
			WORD16 *temp_16;

			WORD16 PixelMax;

			if (StrPtr->PtrHeader->Header.Part4.PixelBitDepth_4Bits == 0)
				PixelMax = (1 << 16) - 1;
			else
				PixelMax = (1 << StrPtr->PtrHeader->Header.Part4.PixelBitDepth_4Bits) - 1;


			temp_16 = (WORD16 *)calloc(sizeof(WORD16), StrPtr->ImageWidth);

			// if(StrPtr->PixelByteOrder == 1)
			if ( StrPtr->PixelByteOrder != machineendianness)  //  bug fix (Kiely)
				for(r = 0; r < actual_rows; r++)
				{
					for(i = 0; i < StrPtr->ImageWidth; i++)
					{
						image[r][i] = (image[r][i] > PixelMax) ? PixelMax : image[r][i];
						image[r][i] = (image[r][i] < 0) ? 0 : image[r][i];
						image[r][i] = ((image[r][i] << 8) & MSBmask) + (image[r][i] >> 8) ;  //  bug fix (Kiely)
						temp_16[i] =(WORD16)image[r][i] ;
					}
					fwrite(temp_16, StrPtr->ImageWidth, sizeof(WORD16), outfile);
				}
			else
			{
				for(r = 0; r < actual_rows; r++)
				{
					for(i = 0; i < StrPtr->ImageWidth; i++)
					{
						image[r][i] = (image[r][i] > PixelMax) ? PixelMax : image[r][i];
						image[r][i] = (image[r][i] < 0) ? 0 : image[r][i];
						temp_16[i] = (WORD16)image[r][i] ;
					}
					fwrite(temp_16, StrPtr->ImageWidth, sizeof(WORD16), outfile);
				}
			}
			free(temp_16);
		}
		else
		{
			short *temp_16 = (short *)calloc(sizeof(short), StrPtr->ImageWidth);

			WORD16 PixelMax = 0;
			int PixelMin = 0;

			if (StrPtr->PtrHeader->Header.Part4.PixelBitDepth_4Bits == 0)
			{
				PixelMax = (1 << 15) - 1;
			}
			else
			{
				PixelMax = (1 << (StrPtr->PtrHeader->Header.Part4.PixelBitDepth_4Bits - 1)) - 1;
			}

			PixelMin = - PixelMax - 1;

			// if(StrPtr->PixelByteOrder == 1)
			if ( StrPtr->PixelByteOrder != machineendianness)  //  bug fix (Kiely)
			{
				for(r = 0; r < actual_rows; r++)
				{
					for(i = 0; i < StrPtr->ImageWidth; i++)
					{
						image[r][i] = (image[r][i] > PixelMax) ? PixelMax : image[r][i];
						image[r][i] = (image[r][i] < PixelMin) ? PixelMin : image[r][i];
						image[r][i] = ((image[r][i] << 8) & MSBmask) + (image[r][i] >> 8);  //  bug fix (Kiely)
						temp_16[i] = (short)image[r][i] ;
					}
					fwrite(temp_16, StrPtr->ImageWidth, sizeof(short), outfile);
				}
			}
			else
			{
				for(r = 0; r < actual_rows; r++)
				{
					for(i = 0; i < StrPtr->ImageWidth; i++)
					{

						image[r][i] = (image[r][i] > PixelMax) ? PixelMax : image[r][i];
						image[r][i] = (image[r][i] < PixelMin) ? PixelMin : image[r][i];
						temp_16[i] =(short)image[r][i] ;
					}
					fwrite(temp_16, StrPtr->ImageWidth, sizeof(short), outfile);
				}
			}
			free(temp_16);
		}
	}
	fclose(outfile);
	return BPE_OK;
}


void DecodingOutputInteger(StructCodingPara *PtrCP,
					int **imgout_integercase)
{
	CoeffDegroup(imgout_integercase, PtrCP->ImageRows, PtrCP->ImageWidth + PtrCP->PadCols_3Bits);

	DWT_Reverse(imgout_integercase, PtrCP);

	if (PtrCP->PtrHeader->Header.Part4.TransposeImg == TRANSPOSE)
	{
		UINT32 i = 0;
		UINT32 j = 0;
		int **transposedimg = NULL;
        UINT32 originalRows = PtrCP->ImageRows;
        UINT32 originalWidth = PtrCP->ImageWidth;

        // ★★★ 変更点(1): 転置後の正しいサイズ (Width x Rows) でメモリを確保 ★★★
		transposedimg = (int **)calloc(originalWidth, sizeof(int *));
		if (transposedimg == NULL) ErrorMsg(BPE_MEM_ERROR);

		for(i = 0; i < originalWidth; i++) {
			transposedimg[i] = (int *)calloc(originalRows, sizeof(int));
			if (transposedimg[i] == NULL) ErrorMsg(BPE_MEM_ERROR);
		}

		// 転置処理: imgout_integercase[i][j] -> transposedimg[j][i]
		for( i = 0 ; i < originalRows; i ++)
			for(j = 0; j < originalWidth; j++)
				transposedimg[j][i] = imgout_integercase[i][j];

		// ★★★ 変更点(2): ImageWriteに転置後の次元を伝えるため、値をスワップ ★★★
        PtrCP->ImageRows = originalWidth;
        PtrCP->ImageWidth = originalRows;

		ImageWrite(PtrCP, transposedimg);

		// ★★★ 変更点(3): スワップした値を元に戻す ★★★
        PtrCP->ImageRows = originalRows;
        PtrCP->ImageWidth = originalWidth;

		// ★★★ 変更点(4): 確保した正しいサイズで解放 ★★★
		for(i = 0; i < originalWidth; i++) {
			free(transposedimg[i]);
		}
		free(transposedimg);
	}
	else
		ImageWrite(PtrCP, imgout_integercase);

	return;
}

short ImageWriteFloat(StructCodingPara *StrPtr,
				 float **image)
{
	UINT32 r = 0;
	UINT32 i = 0;
	FILE *outfile;
	UCHAR8 machineendianness;  // indicates endian-ness of the computer -- bug fix (Kiely)
	unsigned long int bigendtest = 1;  //  bug fix (Kiely)

	UINT32 actual_rows;
	if (StrPtr->ImageRows >= StrPtr->PtrHeader->Header.Part1.PadRows_3Bits) {
		actual_rows = StrPtr->ImageRows - StrPtr->PtrHeader->Header.Part1.PadRows_3Bits;
	} else {
		// ヘッダが不正でパディング行数が画像行数より多い場合、行数を0として扱う
		actual_rows = 0;
		fprintf(stderr, "Warning: Corrupted header. PadRows (%u) > ImageRows (%u). Outputting 0 rows.\n",
		        StrPtr->PtrHeader->Header.Part1.PadRows_3Bits, StrPtr->ImageRows);
	}

	if((outfile = fopen(StrPtr->CodingOutputFile,"wb")) == NULL)
		ErrorMsg(BPE_FILE_ERROR);

	if(StrPtr->PtrHeader->Header.Part4.PixelBitDepth_4Bits <= 8
		&& StrPtr->PtrHeader->Header.Part4.PixelBitDepth_4Bits != 0)
	{
		if(StrPtr->PtrHeader->Header.Part4.SignedPixels == FALSE) // unsigned image
		{
			UCHAR8 *temp = (UCHAR8 *)calloc(sizeof(UCHAR8), StrPtr->ImageWidth);

			for(r = 0; r< actual_rows; r++)
			{
				for(i = 0; i < StrPtr->ImageWidth; i++)
				{
					image[r][i] = (image[r][i] > 0xFF)?0xFF :image[r][i] ;
					image[r][i] = (image[r][i] < 0)?0 :image[r][i] ;
					temp[i] = (UCHAR8)image[r][i];
				}
				fwrite(temp, StrPtr->ImageWidth, sizeof(char),outfile);
			}
			free(temp);
		}
		else
		{
			char *temp = (char *)calloc(sizeof(char), StrPtr->ImageWidth);

			for(r=0; r< actual_rows; r++)
			{
				for(i = 0; i < StrPtr->ImageWidth; i++)
				{
					image[r][i] = (image[r][i] > 127)?127 :image[r][i] ;
					image[r][i] = (image[r][i] <-128 )?-128 :image[r][i] ;
					temp[i] = (char)image[r][i];
				}
				fwrite(temp, StrPtr->ImageWidth, sizeof(char),outfile);
			}
			free(temp);
		}
	}
	else if(StrPtr->PtrHeader->Header.Part4.PixelBitDepth_4Bits == 0 ||
		StrPtr->PtrHeader->Header.Part4.PixelBitDepth_4Bits <= 15) // it is 16 bits
	{
		// machineendianness will be 1 if computer is big-endian, or 0 if little-endian
		const unsigned short MSBmask=0xFF00;  //  bug fix (Kiely)
		machineendianness = (((char *)&bigendtest)[0]==0);  //  bug fix (Kiely)

		if(StrPtr->PtrHeader->Header.Part4.SignedPixels == FALSE) // unsigned image
		{
			WORD16 PixelMax;
			WORD16 *temp_16 = (WORD16 *)calloc(sizeof(WORD16), StrPtr->ImageWidth);

			if (StrPtr->PtrHeader->Header.Part4.PixelBitDepth_4Bits == 0)
				PixelMax = (1 << 16) - 1;
			else
				PixelMax = (1 << StrPtr->PtrHeader->Header.Part4.PixelBitDepth_4Bits) - 1;

			// if(StrPtr->PixelByteOrder == 1)
			if ( StrPtr->PixelByteOrder != machineendianness)  //  bug fix (Kiely)
				for(r = 0; r < actual_rows; r++)
				{
					for(i = 0; i < StrPtr->ImageWidth; i++)
					{
						image[r][i] = (image[r][i] > PixelMax) ? PixelMax : image[r][i];
						image[r][i] = (image[r][i] < 0) ? 0 : image[r][i];
						image[r][i] = (float)( ( ((int)(image[r][i])) << 8) & MSBmask ) + (((int)(image[r][i])) >> 8);  //  bug fix (Kiely)
						temp_16[i] = (WORD16)image[r][i] ;
					}
					fwrite(temp_16, StrPtr->ImageWidth, sizeof(WORD16), outfile);
				}
			else
			{
				for(r = 0; r < actual_rows; r++)
				{
					for(i = 0; i < StrPtr->ImageWidth; i++)
					{
						image[r][i] = (image[r][i] > PixelMax) ? PixelMax : image[r][i];
						image[r][i] = (image[r][i] < 0) ? 0 : image[r][i];
						temp_16[i] =(WORD16)image[r][i] ;
					}
					fwrite(temp_16, StrPtr->ImageWidth, sizeof(WORD16), outfile);
				}
			}
			free(temp_16);
		}
		else  // signed image
		{
			short *temp_16 = (short *)calloc(sizeof(short), StrPtr->ImageWidth);

			WORD16 PixelMax = 0;
			int PixelMin = 0;

			if (StrPtr->PtrHeader->Header.Part4.PixelBitDepth_4Bits == 0)
			{
				PixelMax = (1 << 15) - 1;
			}
			else
			{
				PixelMax = (1 << (StrPtr->PtrHeader->Header.Part4.PixelBitDepth_4Bits - 1)) - 1;
			}

			PixelMin = - PixelMax - 1;

			//if(StrPtr->PixelByteOrder == 1)
			if ( StrPtr->PixelByteOrder != machineendianness)  //  bug fix (Kiely)
				for(r = 0; r < actual_rows; r++)
				{
					for(i = 0; i < StrPtr->ImageWidth; i++)
					{
						image[r][i] = (image[r][i] > PixelMax) ? PixelMax : image[r][i];
						image[r][i] = (image[r][i] < PixelMin) ? PixelMin : image[r][i];
						image[r][i] = (float)( ( ((int)(image[r][i])) << 8) & MSBmask ) + (((int)(image[r][i])) >> 8);  //  bug fix (Kiely)
						temp_16[i] = (short)image[r][i] ;
					}
					fwrite(temp_16, StrPtr->ImageWidth, sizeof(short), outfile);
				}
			else
			{
				for(r = 0; r < actual_rows; r++)
				{
					for(i = 0; i < StrPtr->ImageWidth; i++)
					{
						image[r][i] = (image[r][i] > PixelMax) ? PixelMax : image[r][i];
						image[r][i] = (image[r][i] < PixelMin) ? PixelMin : image[r][i];
						temp_16[i] =(short)image[r][i] ;
					}
					fwrite(temp_16, StrPtr->ImageWidth, sizeof(short), outfile);
				}
			}
			free(temp_16);
		}
	}
	fclose(outfile);
	return BPE_OK;
}


void DecodingOutputFloating(StructCodingPara *PtrCP,
					float **imgout_floatingcase)
{
	CoeffDegroupFloating(imgout_floatingcase, PtrCP->ImageRows, PtrCP->ImageWidth + PtrCP->PadCols_3Bits );

	DWT_ReverseFloating(imgout_floatingcase, PtrCP);

	if (PtrCP->PtrHeader->Header.Part4.TransposeImg == TRANSPOSE)
	{
		UINT32 i = 0;
		UINT32 j = 0;
		float **transposedimg = NULL;
        UINT32 originalRows = PtrCP->ImageRows;
        UINT32 originalWidth = PtrCP->ImageWidth;

		// ★★★ 変更点(1): 転置後の正しいサイズ (Width x Rows) でメモリを確保 ★★★
		transposedimg = (float **)calloc(originalWidth, sizeof(float *));
		if (transposedimg == NULL) ErrorMsg(BPE_MEM_ERROR);

		for(i = 0; i < originalWidth; i++) {
			transposedimg[i] = (float *)calloc(originalRows, sizeof(float));
			if (transposedimg[i] == NULL) ErrorMsg(BPE_MEM_ERROR);
		}

		// 転置処理: imgout_floatingcase[i][j] -> transposedimg[j][i]
		for( i = 0 ; i < originalRows; i ++)
			for(j = 0; j < originalWidth; j++)
				transposedimg[j][i] = imgout_floatingcase[i][j];

		// ★★★ 変更点(2): ImageWriteFloatに転置後の次元を伝えるため、値をスワップ ★★★
        PtrCP->ImageRows = originalWidth;
        PtrCP->ImageWidth = originalRows;

		ImageWriteFloat(PtrCP, transposedimg);

		// ★★★ 変更点(3): スワップした値を元に戻す ★★★
        PtrCP->ImageRows = originalRows;
        PtrCP->ImageWidth = originalWidth;

		// ★★★ 変更点(4): 確保した正しいサイズで解放 ★★★
		for(i = 0; i < originalWidth; i++) {
			free(transposedimg[i]);
		}
		free(transposedimg);
	}
	else
		ImageWriteFloat(PtrCP, imgout_floatingcase);

	return;
}
///////////////////////////////////////////////////////////////////////////////

void TempCoeffOutput(FILE *fdc,
					 FILE * fac,
					 BitPlaneBits *BlockCodingInfo,
					 StructCodingPara * PtrCoding)
{
	UINT32 i;
	int totalbytes_counter = 0;

	for(i = 0; i < PtrCoding->PtrHeader->Header.Part3.S_20Bits; i ++)
	{
		int m, n;
//test
		fwrite(&(BlockCodingInfo[i].PtrBlockAddress[0][0]), 1,sizeof(long), fdc);

		for(m = 0; m < 8; m++)
			for(n = 0; n < 8; n++)
			{
				totalbytes_counter += 4;
				fwrite(&(BlockCodingInfo[i].PtrBlockAddress[m][n]), 1,sizeof(long), fac);
			}

	}
}

void SegmentBufferFlushDecoder(StructCodingPara *StrCoding) // flush codes and reset
{

	if((StrCoding->PtrHeader->Header.Part2.SegByteLimit_27Bits != 0)
		&&(StrCoding->SegmentFull == FALSE) && StrCoding->PtrHeader->Header.Part2.UseFill == TRUE)
	{
		DWORD32 temp = 0;
		while(StrCoding->SegmentFull == FALSE)
		{
			BitsRead(StrCoding, &temp, 8);
		}
	}

	StrCoding->Bits->TotalBitCounter  +=  StrCoding->Bits->CodeWordAlighmentBits;
	StrCoding->Bits->SegBitCounter = 0;
	StrCoding->Bits->ByteBuffer_4Bytes = 0;
	StrCoding->Bits->CodeWordAlighmentBits = 0;
	return;
}

void DecoderEngine(StructCodingPara * PtrCoding)
{
	UINT32 i = 0;
	UINT32 j = 0;
	UINT32 X = 0;
	UINT32 Y = 0;
	UINT32 TotalBlocks = 0;

	int **imgout_integercase = NULL;
	float **imgout_floatingcase = NULL;
	StructFreBlockString * StrFreBlockString = NULL;
	StructFreBlockString *tempStr = NULL;
	StructFreBlockString *rootStrFreBlockString = NULL;


	PtrCoding->Bits = (BitStream *)calloc(sizeof(BitStream), 1);
	if (PtrCoding->Bits == NULL) {
		ErrorMsg(BPE_MEM_ERROR);
	}

	if((PtrCoding->Bits->F_Bits = fopen(PtrCoding->InputFile, "rb")) == NULL)
		ErrorMsg(BPE_FILE_ERROR);

	HeaderReadin(PtrCoding);
	PtrCoding->ImageWidth = PtrCoding->PtrHeader->Header.Part4.ImageWidth_20Bits;

	if(PtrCoding->ImageWidth % BLOCK_SIZE != 0)
		PtrCoding->PadCols_3Bits = BLOCK_SIZE - (PtrCoding->ImageWidth % 8 );
	else
		PtrCoding->PadCols_3Bits  =  0;

	StrFreBlockString = (StructFreBlockString *)calloc(sizeof(StructFreBlockString), 1);
	if (StrFreBlockString == NULL) {
		ErrorMsg(BPE_MEM_ERROR);
	}
	rootStrFreBlockString = StrFreBlockString;
	StrFreBlockString->next = NULL;
	StrFreBlockString->previous = NULL;
	PtrCoding->BlockCounter = 0;

	TotalBlocks = 0;
	for(;;)
	{
		BitPlaneBits * BlockCodingInfo = NULL;
		int segment_ok = 1;

		UINT32 blocks_in_segment = PtrCoding->PtrHeader->Header.Part3.S_20Bits;

		if (blocks_in_segment == 0 || blocks_in_segment > 500000) {
			fprintf(stderr, "Warning: Corrupted header detected (S_20Bits = %u is invalid). Skipping segment.\n", blocks_in_segment);
			segment_ok = 0;
		}

		if (PtrCoding->PtrHeader->Header.Part1.BitDepthDC_5Bits == 0) {
			// DCDeCoding/AdjustOutPutがDeConvTwosCompにこの値をleftmostとして渡す。
			// 0だとDeConvTwosCompがErrorMsg(BPE_DATA_ERROR)->abort()でプロセスごと落ちるため、
			// S_20Bits同様ここで弾いてこのセグメントだけスキップする。
			fprintf(stderr, "Warning: Corrupted header detected (BitDepthDC_5Bits = 0 is invalid). Skipping segment.\n");
			segment_ok = 0;
		}

		if (segment_ok) {
			BlockCodingInfo = (BitPlaneBits *)calloc(blocks_in_segment, sizeof(BitPlaneBits));
			if (BlockCodingInfo == NULL) {
				segment_ok = 0;
			}
		}

		if (segment_ok) {
			UINT32 total_rows = blocks_in_segment * BLOCK_SIZE;
			StrFreBlockString->FreqBlkString = (long **)calloc(total_rows, sizeof(long *));
			if (StrFreBlockString->FreqBlkString == NULL) {
				free(BlockCodingInfo);
				segment_ok = 0;
			} else {
				for (i = 0; i < total_rows; i++) {
					StrFreBlockString->FreqBlkString[i] = (long *)calloc(BLOCK_SIZE, sizeof(long));
					if (StrFreBlockString->FreqBlkString[i] == NULL) {
						for (j = 0; j < i; j++) free(StrFreBlockString->FreqBlkString[j]);
						free(StrFreBlockString->FreqBlkString);
						free(BlockCodingInfo);
						segment_ok = 0;
						break;
					}
				}
			}
		}

		if (segment_ok) {
			UINT32 total_rows = blocks_in_segment * BLOCK_SIZE;
			StrFreBlockString->FloatingFreqBlk = (float **)calloc(total_rows, sizeof(float *));
			if (StrFreBlockString->FloatingFreqBlk == NULL) {
				for (i = 0; i < total_rows; i++) free(StrFreBlockString->FreqBlkString[i]);
				free(StrFreBlockString->FreqBlkString);
				free(BlockCodingInfo);
				segment_ok = 0;
			} else {
				for (i = 0; i < total_rows; i++) {
					StrFreBlockString->FloatingFreqBlk[i] = (float *)calloc(BLOCK_SIZE, sizeof(float));
					if (StrFreBlockString->FloatingFreqBlk[i] == NULL) {
						for (j = 0; j < i; j++) free(StrFreBlockString->FloatingFreqBlk[j]);
						free(StrFreBlockString->FloatingFreqBlk);
						for (j = 0; j < total_rows; j++) free(StrFreBlockString->FreqBlkString[j]);
						free(StrFreBlockString->FreqBlkString);
						free(BlockCodingInfo);
						segment_ok = 0;
						break;
					}
				}
			}
		}

		if (segment_ok) {
			TotalBlocks += blocks_in_segment;
			StrFreBlockString->Blocks = blocks_in_segment;
			DCDeCoding(PtrCoding, StrFreBlockString, BlockCodingInfo);
			ACBpeDecoding(PtrCoding, BlockCodingInfo);
			AdjustOutPut(PtrCoding, BlockCodingInfo);
			free(BlockCodingInfo);
			PtrCoding->BlockCounter += blocks_in_segment;
		} else {
			fprintf(stderr, "Skipping packet due to memory allocation failure or invalid header.\n");
			StrFreBlockString->Blocks = 0;
		}

		SegmentBufferFlushDecoder(PtrCoding);
		PtrCoding->SegmentFull = FALSE;
		PtrCoding->RateReached = FALSE;
		PtrCoding->DecodingStopLocations.BitPlaneStopDecoding = 0;

		if(PtrCoding->PtrHeader->Header.Part1.EngImgFlg == TRUE)
			break;

		if (PtrCoding->PtrHeader->Header.Part1.EngImgFlg != TRUE) {
			HeaderReadin(PtrCoding);
			if (feof(PtrCoding->Bits->F_Bits)) {
				break;
			}
		}

		tempStr = (StructFreBlockString *)calloc(sizeof(StructFreBlockString), 1);
		if (tempStr == NULL) {
			ErrorMsg(BPE_MEM_ERROR);
		}
		(StrFreBlockString->next) = tempStr;
		tempStr->previous = StrFreBlockString;
		tempStr->next = NULL;
		StrFreBlockString = StrFreBlockString->next;
	}

	// ★★★★★ 最終的な行数確定ロジック ★★★★★
	UINT32 final_rows = 0;
	UINT32 image_width_with_pad = PtrCoding->ImageWidth + PtrCoding->PadCols_3Bits;

	if (image_width_with_pad > 0 && TotalBlocks > 0) {
		// 1. デコードされた総ピクセル数を計算
		UINT64 total_pixels = (UINT64)TotalBlocks * BLOCK_SIZE * BLOCK_SIZE; // 8*8=64

		// 2. 総ピクセル数から行数を計算
		UINT32 calculated_rows = total_pixels / image_width_with_pad;

		// 3. CoeffDegroup のために、行数を8の倍数に切り捨てる (安全な最大行数を確定)
		final_rows = (calculated_rows / 8) * 8;

		if (PtrCoding->ImageRows != final_rows) {
			fprintf(stderr, "Info: Adjusting ImageRows from %u to a safe value %u based on decoded data and algorithm constraints.\n",
					PtrCoding->ImageRows, final_rows);
		}
	} else {
        // デコードできるデータがなかった場合
        final_rows = 0;
    }

	// 4. 確定した安全な行数をグローバルなパラメータに設定
	PtrCoding->ImageRows = final_rows;
    // ★★★★★ 変更ここまで ★★★★★


	imgout_integercase = (int **)calloc(PtrCoding->ImageRows,sizeof(int *));
	if(imgout_integercase == NULL && PtrCoding->ImageRows > 0) ErrorMsg(BPE_MEM_ERROR);
	for(i = 0; i < PtrCoding->ImageRows; i++) {
		imgout_integercase[i] = (int *)calloc(image_width_with_pad, sizeof(int));
		if(imgout_integercase[i] == NULL) ErrorMsg(BPE_MEM_ERROR);
	}

	imgout_floatingcase = (float **)calloc(PtrCoding->ImageRows,sizeof(float *));
	if(imgout_floatingcase == NULL && PtrCoding->ImageRows > 0) ErrorMsg(BPE_MEM_ERROR);
	for(i = 0; i < PtrCoding->ImageRows; i++) {
		imgout_floatingcase[i] = (float *)calloc(image_width_with_pad, sizeof(float));
		if(imgout_floatingcase[i] == NULL) ErrorMsg(BPE_MEM_ERROR);
	}

	StrFreBlockString = rootStrFreBlockString;
	X = 0; Y = 0;
	while(StrFreBlockString != NULL) {
		if (StrFreBlockString->Blocks > 0) {
			UINT32 F_x = 0;
			do {
				for( i = 0; i < BLOCK_SIZE; i++) {
					for( j = 0; j < BLOCK_SIZE; j++) {
						if (X + i < PtrCoding->ImageRows && Y + j < image_width_with_pad) {
							imgout_integercase[X + i][Y + j] = StrFreBlockString->FreqBlkString[F_x + i][j];
							imgout_floatingcase[X + i][Y + j] = StrFreBlockString->FloatingFreqBlk[F_x + i][j];
						}
					}
				}
				Y += BLOCK_SIZE;
				if( Y >= image_width_with_pad) {
					Y = 0;
					X += BLOCK_SIZE;
				}
				F_x += BLOCK_SIZE;
			} while (X < PtrCoding->ImageRows && F_x < StrFreBlockString->Blocks * BLOCK_SIZE);
		}
		StrFreBlockString = StrFreBlockString->next;
	}

	if(PtrCoding->PtrHeader->Header.Part4.DWTType == INTEGER_WAVELET)
		DecodingOutputInteger(PtrCoding, imgout_integercase);
	else
		DecodingOutputFloating(PtrCoding, imgout_floatingcase);

	fclose(PtrCoding->Bits->F_Bits);

    // (解放処理は前回のままでOK)
	if (PtrCoding->Bits) {
		free(PtrCoding->Bits);
		PtrCoding->Bits = NULL;
	}

	if (imgout_integercase) {
		for(i = 0; i < PtrCoding->ImageRows; i++) {
			if (imgout_integercase[i]) free(imgout_integercase[i]);
		}
		free(imgout_integercase);
	}
	if (imgout_floatingcase) {
		for(i = 0; i < PtrCoding->ImageRows; i++) {
			if (imgout_floatingcase[i]) free(imgout_floatingcase[i]);
		}
		free(imgout_floatingcase);
	}

	StrFreBlockString = rootStrFreBlockString;
	while(StrFreBlockString != NULL) {
		tempStr = StrFreBlockString->next;
		if (StrFreBlockString->Blocks > 0) {
			UINT32 total_rows_to_free = StrFreBlockString->Blocks * BLOCK_SIZE;
			if (StrFreBlockString->FreqBlkString) {
				for (i = 0; i < total_rows_to_free; i++) {
					if (StrFreBlockString->FreqBlkString[i]) free(StrFreBlockString->FreqBlkString[i]);
				}
				free(StrFreBlockString->FreqBlkString);
			}
			if (StrFreBlockString->FloatingFreqBlk) {
				for (i = 0; i < total_rows_to_free; i++) {
					if (StrFreBlockString->FloatingFreqBlk[i]) free(StrFreBlockString->FloatingFreqBlk[i]);
				}
				free(StrFreBlockString->FloatingFreqBlk);
			}
		}
		free(StrFreBlockString);
		StrFreBlockString = tempStr;
	}

	return;
}
