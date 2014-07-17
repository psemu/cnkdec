// cnkdec.c - Forgelight terrain chunk compression/decompression
// adapted from lzham example1

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>

// Define LZHAM_DEFINE_ZLIB_API causes lzham.h to remap the standard zlib.h functions/macro definitions to lzham's.
// This is totally optional - you can also directly use the lzham_* functions and macros instead.
#define LZHAM_DEFINE_ZLIB_API
#include "lzham_static_lib.h"

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint;

#define my_max(a,b) (((a) > (b)) ? (a) : (b))
#define my_min(a,b) (((a) < (b)) ? (a) : (b))

#define BUF_SIZE (1024 * 1024)
static uint8 s_inbuf[BUF_SIZE];
static uint8 s_outbuf[BUF_SIZE];

// 2^26=64MB dictionary size
#define WINDOW_BITS 20

int main(int argc, char *argv[])
{
   const char *pMode;
   FILE *pInfile, *pOutfile;
   uint infile_size;
   
   uint outfile_size;
   uint infile_size_be;
   int level = LZHAM_Z_DEFAULT_COMPRESSION;
   z_stream stream;
   int n = 1;
   const char *pSrc_filename;
   const char *pDst_filename;
   uint cnk_version = 1;
   char fourcc[] = {'C','N','K','0'};

   printf("CNK Compressor/decompressor\nUsing LZHAM library version %s\n", ZLIB_VERSION);
   
   if (argc < 4)
   {
      printf("Usage: cnkdec [mode:c or d] infile outfile\n");
      printf("\nModes:\n");
      printf("c - Compresses file infile to a compressed CNK[0-5] in file outfile\n");
      printf("d - Decompress a compressed CNK[0-5] file to file outfile\n");
      return EXIT_FAILURE;
   }

   if ((argc - n) < 3)
   {
      printf("Must specify mode, input filename, and output filename after options!\n");
      return EXIT_FAILURE;
   }
   else if ((argc - n) > 3)
   {
      printf("Too many filenames!\n");
      return EXIT_FAILURE;
   }

   pMode = argv[n++];
   if (!strchr("cCdD", pMode[0]))
   {
      printf("Invalid mode!\n");
      return EXIT_FAILURE;
   }

   pSrc_filename = argv[n++];
   pDst_filename = argv[n++];

   printf("Mode: %c\nInput File: \"%s\"\nOutput File: \"%s\"\n", pMode[0], pSrc_filename, pDst_filename);
      
   // Open input file.
   pInfile = fopen(pSrc_filename, "rb");
   if (!pInfile)
   {
      printf("Failed opening input file!\n");
      return EXIT_FAILURE;
   }

   // Open output file.
   pOutfile = fopen(pDst_filename, "wb");
   if (!pOutfile)
   {
      printf("Failed opening output file!\n");
      return EXIT_FAILURE;
   }

   fread(&fourcc, 1, 4, pInfile);
   fread(&cnk_version, 1, 4, pInfile);
   fwrite(&fourcc, 1, 4, pOutfile);
   fwrite(&cnk_version, 1, 4, pOutfile);


   if ((pMode[0] == 'c') || (pMode[0] == 'C'))
   {
      // Compression.
      uint infile_remaining;

	  // Determine input file's size.
	  fseek(pInfile, 0, SEEK_END);
	  infile_size = ftell(pInfile) - 8;
	  fseek(pInfile, 8, SEEK_SET);
	  
	  printf("Input file size: %u\n", infile_size);

	  // Init the z_stream
	  memset(&stream, 0, sizeof(stream));
	  stream.next_in = s_inbuf;
	  stream.avail_in = 0;
	  stream.next_out = s_outbuf;
	  stream.avail_out = BUF_SIZE;

      infile_remaining = infile_size;

      if (deflateInit2(&stream, level, LZHAM_Z_LZHAM, WINDOW_BITS, 9, LZHAM_Z_DEFAULT_STRATEGY) != Z_OK)
      {
         printf("deflateInit() failed!\n");
         return EXIT_FAILURE;
      }

	  fwrite(&infile_size, 1, 4, pOutfile);
	  fwrite(&infile_size, 1, 4, pOutfile);

      for ( ; ; )
      {
         int status;
         if (!stream.avail_in)
         {
            // Input buffer is empty, so read more bytes from input file.
            uint n = my_min(BUF_SIZE, infile_remaining);

            if (fread(s_inbuf, 1, n, pInfile) != n)
            {
               printf("Failed reading from input file!\n");
               return EXIT_FAILURE;
            }

            stream.next_in = s_inbuf;
            stream.avail_in = n;

            infile_remaining -= n;
            //printf("Input bytes remaining: %u\n", infile_remaining);
         }

         status = deflate(&stream, infile_remaining ? Z_NO_FLUSH : Z_FINISH);

         if ((status == Z_STREAM_END) || (!stream.avail_out))
         {
            // Output buffer is full, or compression is done, so write buffer to output file.
            uint n = BUF_SIZE - stream.avail_out;
            if (fwrite(s_outbuf, 1, n, pOutfile) != n)
            {
               printf("Failed writing to output file!\n");
               return EXIT_FAILURE;
            }
            stream.next_out = s_outbuf;
            stream.avail_out = BUF_SIZE;
         }

         if (status == Z_STREAM_END)
            break;
         else if (status != Z_OK)
         {
            printf("deflate() failed with status %i!\n", status);
            return EXIT_FAILURE;
         }
      }

      if (deflateEnd(&stream) != Z_OK)
      {
         printf("deflateEnd() failed!\n");
         return EXIT_FAILURE;
      }

	  fseek(pOutfile, 0, SEEK_END);
	  // write the uncompressed length (big-endian) at the end
	  infile_size_be = _byteswap_ulong(infile_size);
	  fwrite(&infile_size_be, 1, 4, pOutfile);

	  outfile_size = ftell(pOutfile) - 16;

	  // seek back to start+12 and write the compressed file length
	  fseek(pOutfile, 12, SEEK_SET);
	  fwrite(&outfile_size, 1, 4, pOutfile);

   }
   else if ((pMode[0] == 'd') || (pMode[0] == 'D'))
   {
      // Decompression.
      uint infile_remaining;

	  // Determine input file's size.
	  fseek(pInfile, 0, SEEK_END);
	  infile_size = ftell(pInfile) - 16;
	  fseek(pInfile, 16, SEEK_SET);

	  printf("Input file size: %u\n", infile_size);

	  // Init the z_stream
	  memset(&stream, 0, sizeof(stream));
	  stream.next_in = s_inbuf;
	  stream.avail_in = 0;
	  stream.next_out = s_outbuf;
	  stream.avail_out = BUF_SIZE;

      infile_remaining = infile_size - 4;

	  if (inflateInit2(&stream, WINDOW_BITS))
      {
         printf("inflateInit() failed!\n");
         return EXIT_FAILURE;
      }

      for ( ; ; )
      {
         int status;
         if (!stream.avail_in)
         {
            // Input buffer is empty, so read more bytes from input file.
            uint n = my_min(BUF_SIZE, infile_remaining);

            if (fread(s_inbuf, 1, n, pInfile) != n)
            {
               printf("Failed reading from input file!\n");
               return EXIT_FAILURE;
            }

            stream.next_in = s_inbuf;
            stream.avail_in = n;

            infile_remaining -= n;
         }

         status = inflate(&stream, Z_SYNC_FLUSH);

         if ((status == Z_STREAM_END) || (!stream.avail_out))
         {
            // Output buffer is full, or decompression is done, so write buffer to output file.
            uint n = BUF_SIZE - stream.avail_out;
            if (fwrite(s_outbuf, 1, n, pOutfile) != n)
            {
               printf("Failed writing to output file!\n");
               return EXIT_FAILURE;
            }
            stream.next_out = s_outbuf;
            stream.avail_out = BUF_SIZE;
         }

         if (status == Z_STREAM_END)
            break;
         else if (status != Z_OK)
         {
            printf("inflate() failed with status %i!\n", status);
            return EXIT_FAILURE;
         }
      }

      if (inflateEnd(&stream) != Z_OK)
      {
         printf("inflateEnd() failed!\n");
         return EXIT_FAILURE;
      }      
   }
   else
   {
      printf("Invalid mode!\n");
      return EXIT_FAILURE;
   }

   fclose(pInfile);
   if (EOF == fclose(pOutfile))
   {
      printf("Failed writing to output file!\n");
      return EXIT_FAILURE;
   }

   printf("Total input bytes: %u\n", stream.total_in);
   printf("Total output bytes: %u\n", stream.total_out);
   printf("Success.\n");
   return EXIT_SUCCESS;
}
